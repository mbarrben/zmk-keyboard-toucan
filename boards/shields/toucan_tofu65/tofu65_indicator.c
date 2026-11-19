/*
 * Tofu65 3.0 RGB indicator LED, driven on the split peripheral.
 *
 * Priority: deep sleep (off) > dongle link flash > idle on battery (off)
 *           > low battery blink > Caps Lock (purple) > off.
 *
 * The Caps Lock state arrives from the dongle as an invocation of the tf65_ind behavior
 * (param1 = HID indicator bitmap), see boards/shields/toucan/tofu65_indicator_relay.c.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_tofu65_indicators

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/hid_indicators.h>
#include <zmk/activity.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/hid_indicators_types.h>

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/events/battery_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define COLOR_OFF 0
#define COLOR_R BIT(0)
#define COLOR_G BIT(1)
#define COLOR_B BIT(2)

#define CAPS_LOCK_COLOR (COLOR_R | COLOR_B)
#define CONNECTED_COLOR COLOR_G
#define DISCONNECTED_COLOR (COLOR_R | COLOR_G)
#define LOW_BATTERY_COLOR COLOR_R

#define LOW_BATTERY_PERCENT 2
#define LOW_BATTERY_PERIOD_MS 2000
#define LOW_BATTERY_ON_MS 200

// Link flash: 3 blinks = 6 phases of 150 ms (on, off, on, off, on, off).
#define FLASH_PHASE_MS 150
#define FLASH_PHASES 6

static const struct device *const led_dev = DEVICE_DT_GET(DT_NODELABEL(indicator_leds));
static const uint32_t led_idx[] = {
    DT_NODE_CHILD_IDX(DT_NODELABEL(indicator_r)),
    DT_NODE_CHILD_IDX(DT_NODELABEL(indicator_g)),
    DT_NODE_CHILD_IDX(DT_NODELABEL(indicator_b)),
};

static zmk_hid_indicators_t indicators;
static enum zmk_activity_state activity = ZMK_ACTIVITY_ACTIVE;
static uint8_t battery_percent = 100; // until the first battery_state_changed
static bool usb_powered;
static bool link_connected;
static uint8_t flash_color = COLOR_OFF;
static int64_t flash_start;

static void set_color(uint8_t color) {
    static int16_t shown = -1;

    if (color == shown || !device_is_ready(led_dev)) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(led_idx); i++) {
        if (color & BIT(i)) {
            led_on(led_dev, led_idx[i]);
        } else {
            led_off(led_dev, led_idx[i]);
        }
    }
    shown = color;
}

static void render(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(render_work, render);

static void render(struct k_work *work) {
    const int64_t now = k_uptime_get();
    int32_t next_ms = -1;
    uint8_t color = COLOR_OFF;

    if (activity == ZMK_ACTIVITY_SLEEP) {
        color = COLOR_OFF;
    } else if (flash_color != COLOR_OFF && now - flash_start < FLASH_PHASES * FLASH_PHASE_MS) {
        const int64_t elapsed = now - flash_start;
        color = ((elapsed / FLASH_PHASE_MS) % 2 == 0) ? flash_color : COLOR_OFF;
        next_ms = FLASH_PHASE_MS - (int32_t)(elapsed % FLASH_PHASE_MS);
    } else {
        flash_color = COLOR_OFF;

        if (activity == ZMK_ACTIVITY_IDLE && !usb_powered) {
            color = COLOR_OFF;
        } else if (battery_percent <= LOW_BATTERY_PERCENT && !usb_powered) {
            const int32_t phase = (int32_t)(now % LOW_BATTERY_PERIOD_MS);
            color = phase < LOW_BATTERY_ON_MS ? LOW_BATTERY_COLOR : COLOR_OFF;
            next_ms = phase < LOW_BATTERY_ON_MS ? LOW_BATTERY_ON_MS - phase
                                                : LOW_BATTERY_PERIOD_MS - phase;
        } else if (indicators & HID_INDICATOR_CAPS_LOCK) {
            color = CAPS_LOCK_COLOR;
        }
    }

    set_color(color);

    if (next_ms >= 0) {
        k_work_reschedule(&render_work, K_MSEC(next_ms));
    }
}

static void request_render(void) { k_work_reschedule(&render_work, K_NO_WAIT); }

static int tofu65_indicator_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *activity_ev = as_zmk_activity_state_changed(eh);
    if (activity_ev) {
        activity = activity_ev->state;
        if (activity == ZMK_ACTIVITY_SLEEP) {
            // activity.c raises SLEEP synchronously right before sys_poweroff(), and GPIO
            // outputs keep their level in System OFF: switch the LED off here, not in work.
            k_work_cancel_delayable(&render_work);
            set_color(COLOR_OFF);
        } else {
            request_render();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_split_peripheral_status_changed *link_ev =
        as_zmk_split_peripheral_status_changed(eh);
    if (link_ev) {
        // Failed connection attempts also raise connected=false; flash only on transitions.
        if (link_ev->connected != link_connected) {
            link_connected = link_ev->connected;
            flash_color = link_connected ? CONNECTED_COLOR : DISCONNECTED_COLOR;
            flash_start = k_uptime_get();
            request_render();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    const struct zmk_battery_state_changed *battery_ev = as_zmk_battery_state_changed(eh);
    if (battery_ev) {
        battery_percent = battery_ev->state_of_charge;
        request_render();
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev) {
        usb_powered = usb_ev->conn_state != ZMK_USB_CONN_NONE;
        request_render();
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tofu65_indicator, tofu65_indicator_listener);
ZMK_SUBSCRIPTION(tofu65_indicator, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(tofu65_indicator, zmk_split_peripheral_status_changed);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(tofu65_indicator, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(tofu65_indicator, zmk_usb_conn_state_changed);
#endif

static int tf65_ind_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    indicators = (zmk_hid_indicators_t)binding->param1;
    LOG_DBG("HID indicators from dongle: 0x%02x", indicators);
    request_render();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int tf65_ind_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api tf65_ind_driver_api = {
    .binding_pressed = tf65_ind_pressed,
    .binding_released = tf65_ind_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tf65_ind_driver_api);

// APPLICATION level: the gpio-leds device (LED_INIT_PRIORITY, POST_KERNEL) is ready by then.
static int tofu65_indicator_init(void) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    usb_powered = zmk_usb_is_powered();
#endif
    request_render();
    return 0;
}

SYS_INIT(tofu65_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
