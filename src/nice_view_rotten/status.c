/*
 *
 * Based on ZMK's nice_view shield (central screen): the WPM graph is
 * replaced by the classic bongo cat: it taps the table on every key press
 * with the paw on the matching side as in a mirror (left half of the keyboard
 * = paw on the right of the screen, the cat's left paw). The paw goes down
 * as soon as a key is pressed and stays down while that key (the last one
 * pressed) is held; a second press on the same side lifts it for a moment
 * first, so a double letter or a hammered key shows every tap. It goes back
 * to its idle animation when typing stops.
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

LV_IMG_DECLARE(bongo_idle0);
LV_IMG_DECLARE(bongo_idle1);
LV_IMG_DECLARE(bongo_idle2);
LV_IMG_DECLARE(bongo_idle3);
LV_IMG_DECLARE(bongo_idle4);
LV_IMG_DECLARE(bongo_prep0);
LV_IMG_DECLARE(bongo_tap_left);
LV_IMG_DECLARE(bongo_tap_right);

static const lv_img_dsc_t *const bongo_idle[] = {&bongo_idle0, &bongo_idle1, &bongo_idle2,
                                                 &bongo_idle3, &bongo_idle4};
/* Indexed by bongo_frame in tap mode */
static const lv_img_dsc_t *const bongo_tap[] = {&bongo_tap_left, &bongo_tap_right};
#define BONGO_SCREEN_LEFT 0
#define BONGO_SCREEN_RIGHT 1

#define BONGO_FRAME_MS 200 /* idle animation speed */
#define BONGO_TAP_MS 100   /* paw down at least this long, even for a very short press */
#define BONGO_LIFT_MS 60   /* paws-ready pose before a new tap with the paw already down */
#define BONGO_IDLE_MS 1500 /* back to the idle animation this long after the last press */

enum bongo_mode { BONGO_IDLE, BONGO_PREP, BONGO_LIFT, BONGO_TAP };

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

struct bongo_status_state {
    uint32_t key_presses;
    uint32_t last_position; /* last pressed key */
    uint8_t last_side;      /* screen side of its paw */
    bool last_held;         /* still held down */
};

/* Lily58 key positions (keymap order): rows 0-2 have 6 + 6 keys, row 3 has
 * 7 + 7 (with the inner keys), then 4 + 4 thumb keys */
static uint8_t bongo_tap_frame(uint32_t position) {
    bool right_half;
    if (position < 36) {
        right_half = position % 12 >= 6;
    } else if (position < 50) {
        right_half = position - 36 >= 7;
    } else {
        right_half = position - 50 >= 4;
    }
    // Mirror: the left half is the paw on the right of the screen
    return right_half ? BONGO_SCREEN_LEFT : BONGO_SCREEN_RIGHT;
}

/* One-shot timer ending the current tap (-> paws ready) or lift (-> tap) */
static lv_timer_t *bongo_anim_timer;

static void bongo_anim_start(uint32_t ms) {
    lv_timer_set_period(bongo_anim_timer, ms);
    lv_timer_reset(bongo_anim_timer);
    lv_timer_resume(bongo_anim_timer);
}

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
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_WIFI);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw the bongo cat (68x34) where the WPM graph used to be
    const lv_img_dsc_t *frame;
    switch (state->bongo_mode) {
    case BONGO_TAP:
        frame = bongo_tap[state->bongo_frame % ARRAY_SIZE(bongo_tap)];
        break;
    case BONGO_PREP:
    case BONGO_LIFT:
        frame = &bongo_prep0;
        break;
    default:
        frame = bongo_idle[state->bongo_frame % ARRAY_SIZE(bongo_idle)];
        break;
    }
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    lv_canvas_draw_img(canvas, 0, 26, frame, &img_dsc);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw circles
    int circle_offsets[NICEVIEW_PROFILE_COUNT][2] = {
        {13, 13}, {55, 13}, {34, 34}, {13, 55}, {55, 55},
    };

    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; i++) {
        bool selected = i == state->active_profile_index;

        if (state->profiles_connected[i]) {
            lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13, 0, 360,
                               &arc_dsc);
        } else if (state->profiles_bonded[i]) {
            const int segments = 8;
            const int gap = 20;
            for (int j = 0; j < segments; ++j)
                lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13,
                                   360. / segments * j + gap / 2.0,
                                   360. / segments * (j + 1) - gap / 2.0, &arc_dsc);
        }

        if (selected) {
            lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 9, 0, 359,
                               &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        lv_canvas_draw_text(canvas, circle_offsets[i][0] - 8, circle_offsets[i][1] - 10, 16,
                            (selected ? &label_dsc_black : &label_dsc), label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw layer
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};

        sprintf(text, "LAYER %i", state->layer_index);

        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
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
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
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

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; ++i) {
        widget->state.profiles_connected[i] = state->profiles_connected[i];
        widget->state.profiles_bonded[i] = state->profiles_bonded[i];
    }

    draw_top(widget->obj, widget->cbuf, &widget->state);
    draw_middle(widget->obj, widget->cbuf2, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
    for (int i = 0; i < MIN(NICEVIEW_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); ++i) {
        state.profiles_connected[i] = zmk_ble_profile_is_connected(i);
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

// Tap time over: paw up, unless the last pressed key is still held
static void bongo_tap_done(struct zmk_widget_status *widget) {
    struct status_state *s = &widget->state;
    s->bongo_tap_done = true;
    if (s->bongo_last_held) {
        return; // held: the paw stays down until the release
    }
    s->bongo_mode = BONGO_PREP;
    s->last_press = k_uptime_get(); // idle BONGO_IDLE_MS after the paw is lifted
    draw_top(widget->obj, widget->cbuf, s);
}

static void bongo_tap(struct zmk_widget_status *widget, uint8_t side) {
    struct status_state *s = &widget->state;
    s->bongo_mode = BONGO_TAP;
    s->bongo_frame = side;
    s->bongo_tap_done = false;
    bongo_anim_start(BONGO_TAP_MS);
    draw_top(widget->obj, widget->cbuf, s);
}

static void set_bongo_status(struct zmk_widget_status *widget, struct bongo_status_state state) {
    struct status_state *s = &widget->state;
    bool pressed = state.key_presses != s->key_presses;

    s->key_presses = state.key_presses;
    s->bongo_last_held = state.last_held;

    if (!pressed) {
        // The last pressed key was released: paw up once its tap was shown
        if (!state.last_held && s->bongo_mode == BONGO_TAP && s->bongo_tap_done) {
            bongo_tap_done(widget);
        }
        return;
    }

    s->last_press = k_uptime_get();
    if (s->bongo_mode == BONGO_LIFT && s->bongo_frame == state.last_side) {
        return; // the lift in progress ends with a tap anyway
    }
    if (s->bongo_mode == BONGO_TAP && s->bongo_frame == state.last_side) {
        // Same paw already down: lift it for a moment, then tap again
        s->bongo_mode = BONGO_LIFT;
        bongo_anim_start(BONGO_LIFT_MS);
        draw_top(widget->obj, widget->cbuf, s);
        return;
    }
    bongo_tap(widget, state.last_side);
}

static void bongo_status_update_cb(struct bongo_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_bongo_status(widget, state); }
}

// Key positions of both halves reach the central (left half) as position events
static struct bongo_status_state bongo_status_get_state(const zmk_event_t *eh) {
    static struct bongo_status_state state;
    // eh is NULL when the widget initializes
    const struct zmk_position_state_changed *ev = eh ? as_zmk_position_state_changed(eh) : NULL;
    if (ev != NULL) {
        if (ev->state) {
            state.key_presses++;
            state.last_position = ev->position;
            state.last_side = bongo_tap_frame(ev->position);
            state.last_held = true;
        } else if (ev->position == state.last_position) {
            state.last_held = false; // releases of older keys do not matter
        }
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_status, struct bongo_status_state,
                            bongo_status_update_cb, bongo_status_get_state)
ZMK_SUBSCRIPTION(widget_bongo_status, zmk_position_state_changed);

// End of the lift before a new tap (-> tap), or of the minimum tap time
static void bongo_anim_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_status *widget = timer->user_data;
    lv_timer_pause(timer);
    if (widget->state.bongo_mode == BONGO_LIFT) {
        bongo_tap(widget, widget->state.bongo_frame);
    } else if (widget->state.bongo_mode == BONGO_TAP && !widget->state.bongo_tap_done) {
        bongo_tap_done(widget);
    }
}

// Idle animation, and back to it a while after the last press
static void bongo_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_status *widget = timer->user_data;
    struct status_state *state = &widget->state;

    if (state->bongo_mode == BONGO_PREP) {
        if (k_uptime_get() - state->last_press < BONGO_IDLE_MS) {
            return;
        }
        state->bongo_mode = BONGO_IDLE;
        state->bongo_frame = 0;
    } else if (state->bongo_mode == BONGO_IDLE) {
        state->bongo_frame = (state->bongo_frame + 1) % ARRAY_SIZE(bongo_idle);
    } else {
        return; // tap or lift: handled by the animation timer
    }
    draw_top(widget->obj, widget->cbuf, state);
}

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
    widget_bongo_status_init();
    widget->state.bongo_mode = BONGO_IDLE;
    lv_timer_create(bongo_timer_cb, BONGO_FRAME_MS, widget);
    bongo_anim_timer = lv_timer_create(bongo_anim_timer_cb, BONGO_TAP_MS, widget);
    lv_timer_pause(bongo_anim_timer);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
