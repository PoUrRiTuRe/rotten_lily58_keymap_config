/*
 * Glasses style of the bongo cat, saved as "rotten/glasses/style".
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include "glasses.h"

static uint8_t style = ROTTEN_GLASSES_NERD;

uint8_t rotten_glasses_style(void) { return style; }

void rotten_glasses_toggle(void) {
    style = (style == ROTTEN_GLASSES_NERD) ? ROTTEN_GLASSES_THUG : ROTTEN_GLASSES_NERD;
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_save_one("rotten/glasses/style", &style, sizeof(style));
#endif
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int glasses_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                void *cb_arg) {
    if (strcmp(name, "style") == 0 && len == sizeof(style)) {
        uint8_t saved;
        if (read_cb(cb_arg, &saved, sizeof(saved)) == sizeof(saved) &&
            saved <= ROTTEN_GLASSES_THUG) {
            style = saved;
        }
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(rotten_glasses, "rotten/glasses", NULL, glasses_settings_set, NULL,
                               NULL);
#endif
