/*
 *
 * Based on ZMK's nice_view shield (peripheral screen): the battery and link
 * status are unchanged, the balloon / mountain art is replaced by a spinning
 * wireframe cube (light lines on the dark screen, like the stock texts).
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <math.h>

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

/* Cube animation: one frame every CUBE_FRAME_MS; per frame it turns CUBE_STEP_Y
 * hundredths of a degree around Y (one turn every 7.2 s) and CUBE_STEP_X around
 * X (one turn every 14.4 s). Each angle wraps after a full turn, so the motion
 * never jumps, and the whole tumble repeats every 14.4 s. */
#define CUBE_FRAME_MS 50
#define CUBE_STEP_Y 250
#define CUBE_STEP_X 125
#define CUBE_TILT_X 2860 /* starting X angle: the cube starts tilted */
/* The cube canvas sits 37 px from the bottom of the screen: centered in the
 * space under the battery / link row (it covers the empty lower part of the
 * status canvas) */
#define CUBE_X 37

static const int8_t cube_vertices[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                           {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
static const uint8_t cube_edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

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

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

// Wireframe cube, turned by state.cube_angle_y / cube_angle_x (hundredths of a degree)
static void draw_cube(struct zmk_widget_status *widget) {
    lv_obj_t *canvas = widget->cube;

    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_dsc);

    // Turn around Y, then around X at a different speed: the cube tumbles
    float a = widget->state.cube_angle_y * 3.14159265f / 18000.0f;
    float b = widget->state.cube_angle_x * 3.14159265f / 18000.0f;
    float sa = sinf(a), ca = cosf(a), sb = sinf(b), cb = cosf(b);

    lv_point_t points[8];
    for (int i = 0; i < 8; i++) {
        float x = cube_vertices[i][0], y = cube_vertices[i][1], z = cube_vertices[i][2];
        float x1 = x * ca + z * sa;
        float z1 = -x * sa + z * ca;
        float y2 = y * cb - z1 * sb;
        float z2 = y * sb + z1 * cb;
        // Perspective: as big as possible while every corner stays inside the
        // canvas (max sqrt(3 - z^2) / (z + 5) = 0.37, times 88 = 32.5 px)
        float f = 88.0f / (z2 + 5.0f);
        points[i].x = CANVAS_SIZE / 2 + (lv_coord_t)lroundf(x1 * f);
        points[i].y = CANVAS_SIZE / 2 + (lv_coord_t)lroundf(y2 * f);
    }

    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);
    for (int i = 0; i < 12; i++) {
        lv_point_t edge[2] = {points[cube_edges[i][0]], points[cube_edges[i][1]]};
        lv_canvas_draw_line(canvas, edge, 2, &line_dsc);
    }

    rotate_canvas(canvas, widget->cbuf2);
}

static void cube_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_status *widget = timer->user_data;
    widget->state.cube_angle_y = (widget->state.cube_angle_y + CUBE_STEP_Y) % 36000;
    widget->state.cube_angle_x = (widget->state.cube_angle_x + CUBE_STEP_X) % 36000;
    draw_cube(widget);
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

    // Cube canvas, created last so it is drawn over the empty part of the
    // status canvas
    widget->cube = lv_canvas_create(widget->obj);
    lv_obj_align(widget->cube, LV_ALIGN_TOP_LEFT, CUBE_X, 0);
    lv_canvas_set_buffer(widget->cube, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE,
                         LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    widget->state.cube_angle_y = 0;
    widget->state.cube_angle_x = CUBE_TILT_X;
    draw_cube(widget);
    lv_timer_create(cube_timer_cb, CUBE_FRAME_MS, widget);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
