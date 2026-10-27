/*
 * Glasses style of the bongo cat (left screen), kept in the settings so it
 * survives a restart.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define ROTTEN_GLASSES_NERD 0 /* rectangular glasses, the default */
#define ROTTEN_GLASSES_THUG 1 /* easter egg */

uint8_t rotten_glasses_style(void);
void rotten_glasses_toggle(void);
