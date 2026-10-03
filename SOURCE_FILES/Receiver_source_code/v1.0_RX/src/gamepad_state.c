// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * gamepad_state.c
 */

#include "gamepad_state.h"
#include <string.h>

void gamepad_state_reset(gamepad_state_t *state) {
    memset(state, 0, sizeof(gamepad_state_t));
}
