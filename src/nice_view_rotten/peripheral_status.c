/*
 *
 * Based on ZMK's nice_view shield (peripheral screen): the balloon / mountain
 * art is replaced by a rotten flesh under the battery and a beating atom
 * (two quick beats, then a rest) at the bottom of the screen.
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(rotten_flesh);

/* Heartbeat: atom size in percent, one step every BEAT_STEP_MS.
 * Two beats ("lub-dub"), then a rest: about 40 beats per minute. */
#define BEAT_STEP_MS 120
static const uint8_t beat_pattern[] = {100, 118, 104, 118, 100, 100,
                                       100, 100, 100, 100, 100, 100};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Draw the rotten flesh (32x32) under the battery / connection row
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    lv_canvas_draw_img(canvas, 18, 28, &rotten_flesh, &img_dsc);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

// Atom / trefoil: three 60° blades around a dot, scaled to scale_pct percent
static void draw_atom(lv_obj_t *widget, lv_color_t cbuf[], uint8_t scale_pct) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    const lv_coord_t c = CANVAS_SIZE / 2;
    const lv_coord_t r_out = 26 * scale_pct / 100;
    const lv_coord_t r_in = 8 * scale_pct / 100;
    const lv_coord_t r_dot = 5 * scale_pct / 100;

    lv_draw_arc_dsc_t blade_dsc;
    init_arc_dsc(&blade_dsc, LVGL_FOREGROUND, r_out - r_in);
    blade_dsc.rounded = 0;
    // Angles go clockwise from 3 o'clock: two blades up, one down
    lv_canvas_draw_arc(canvas, c, c, r_out, 180, 240, &blade_dsc);
    lv_canvas_draw_arc(canvas, c, c, r_out, 300, 360, &blade_dsc);
    lv_canvas_draw_arc(canvas, c, c, r_out, 60, 120, &blade_dsc);

    lv_draw_arc_dsc_t dot_dsc;
    init_arc_dsc(&dot_dsc, LVGL_FOREGROUND, r_dot);
    lv_canvas_draw_arc(canvas, c, c, r_dot, 0, 360, &dot_dsc);

    rotate_canvas(canvas, cbuf);
}

static void beat_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_status *widget = timer->user_data;
    uint8_t prev = beat_pattern[widget->state.beat_step];
    widget->state.beat_step = (widget->state.beat_step + 1) % ARRAY_SIZE(beat_pattern);
    uint8_t next = beat_pattern[widget->state.beat_step];
    if (next != prev) {
        draw_atom(widget->obj, widget->cbuf2, next);
    }
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *atom = lv_canvas_create(widget->obj);
    lv_obj_align(atom, LV_ALIGN_TOP_LEFT, 6, 0);
    lv_canvas_set_buffer(atom, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    widget->state.beat_step = 0;
    draw_atom(widget->obj, widget->cbuf2, beat_pattern[0]);
    lv_timer_create(beat_timer_cb, BEAT_STEP_MS, widget);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
