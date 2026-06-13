#include <zephyr/kernel.h>
#include <lvgl.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

LV_IMG_DECLARE(toucan128);
LV_IMG_DECLARE(toucan_sleep);

static lv_obj_t *status_img;

void setup_status_screen(lv_obj_t *screen) {
    status_img = lv_img_create(screen);
    lv_img_set_src(status_img, &toucan128);
    lv_obj_center(status_img);
}

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    setup_status_screen(screen);
    return screen;
}

// Swap the displayed image on activity changes. This is a plain (synchronous) listener
// on purpose: when the keyboard deep-sleeps, ZMK's activity_work_handler raises this
// event and then calls sys_poweroff() inline, so the redraw must be flushed to the
// display before the handler returns. A deferred display-work-queue update would never
// run in time. The Sharp memory LCD retains the sleep frame with no power; waking is a
// cold boot that recreates the screen with the toucan image.
static int display_activity_event_handler(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }
    if (status_img == NULL) {
        return 0;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_SLEEP:
        lv_img_set_src(status_img, &toucan_sleep);
        lv_obj_center(status_img);
        // Force LVGL to render and flush to the LCD before the CPU powers off.
        lv_task_handler();
        lv_refr_now(NULL);
        break;
    case ZMK_ACTIVITY_ACTIVE:
        lv_img_set_src(status_img, &toucan128);
        lv_obj_center(status_img);
        break;
    default:
        break; // ignore IDLE
    }
    return 0;
}

ZMK_LISTENER(toucan_pet_display, display_activity_event_handler);
ZMK_SUBSCRIPTION(toucan_pet_display, zmk_activity_state_changed);
