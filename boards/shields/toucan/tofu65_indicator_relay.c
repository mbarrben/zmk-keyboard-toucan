/*
 * Forwards the host's HID indicator state (Caps Lock etc.) from the dongle to the Tofu65 by
 * invoking its tf65_ind behavior (boards/shields/toucan_tofu65/tofu65_indicator.c).
 *
 * Not CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS: zmk_split_central_update_hid_indicator()
 * only reaches the first connected peripheral slot, and never resends to a peripheral that
 * reconnects.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid_indicators_types.h>
#include <zmk/split/central.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Must match the tf65_ind node name in toucan_tofu65.overlay.
#define TOFU65_INDICATOR_BEHAVIOR "tf65_ind"
// Tofu65 keys are positions 42-108 of toucan_tofu65_layout (toucan-tofu65-layout.dtsi).
#define TOFU65_FIRST_POSITION 42
// A Tofu65 that rebooted (deep-sleep wake, power switch) has lost the state; resend it on
// its key presses, at most this often.
#define RESYNC_INTERVAL_MS 10000

static zmk_hid_indicators_t current_indicators;
static uint8_t last_resync_source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
static int64_t last_resync_ms;

static void send_indicators(uint8_t source) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = TOFU65_INDICATOR_BEHAVIOR,
        .param1 = current_indicators,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    int err = zmk_split_central_invoke_behavior(source, &binding, event, true);
    if (err < 0) {
        LOG_WRN("Failed to send indicators to peripheral %d (%d)", source, err);
    }
}

static int tofu65_indicator_relay_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ind_ev = as_zmk_hid_indicators_changed(eh);
    if (ind_ev) {
        current_indicators = ind_ev->indicators;
        // Slot order follows connection order, so send to every slot; the Toucan halves
        // have no tf65_ind behavior and drop it, unconnected slots are skipped by ZMK.
        for (uint8_t source = 0; source < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; source++) {
            send_indicators(source);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev && pos_ev->state && pos_ev->position >= TOFU65_FIRST_POSITION &&
        pos_ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        const int64_t now = k_uptime_get();
        if (pos_ev->source != last_resync_source ||
            now - last_resync_ms >= RESYNC_INTERVAL_MS) {
            last_resync_source = pos_ev->source;
            last_resync_ms = now;
            send_indicators(pos_ev->source);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tofu65_indicator_relay, tofu65_indicator_relay_listener);
ZMK_SUBSCRIPTION(tofu65_indicator_relay, zmk_hid_indicators_changed);
ZMK_SUBSCRIPTION(tofu65_indicator_relay, zmk_position_state_changed);
