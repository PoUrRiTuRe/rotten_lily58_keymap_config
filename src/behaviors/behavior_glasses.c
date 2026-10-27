/*
 * &glasses: switch the bongo cat glasses (rectangular / thug). Only the left
 * screen changes; the key press itself sends nothing to the computer.
 * Based on ZMK's behavior_reset.c (Copyright (c) 2020 The ZMK Contributors)
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT rotten_behavior_glasses

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "../nice_view_rotten/glasses.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_glasses_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    rotten_glasses_toggle();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_glasses_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_glasses_driver_api = {
    .binding_pressed = on_glasses_pressed,
    .binding_released = on_glasses_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define GLASSES_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_glasses_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GLASSES_INST)

#endif
