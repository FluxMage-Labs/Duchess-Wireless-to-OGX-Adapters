// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * gamepad_state.h - Shared state between USB host and BLE output
 */

#ifndef GAMEPAD_STATE_H
#define GAMEPAD_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool a, b, x, y;
    bool left_bumper, right_bumper;
    bool left_thumb, right_thumb;
    bool start, back, guide, sync;
    bool dpad_up, dpad_down, dpad_left, dpad_right;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t left_stick_x, left_stick_y;
    int16_t right_stick_x, right_stick_y;
    bool connected;
    bool updated;
    uint32_t last_report_ms;
} gamepad_state_t;

static inline void gamepad_state_reset(gamepad_state_t *state) {
    state->a = false; state->b = false; state->x = false; state->y = false;
    state->left_bumper = false; state->right_bumper = false;
    state->left_thumb = false; state->right_thumb = false;
    state->start = false; state->back = false;
    state->guide = false; state->sync = false;
    state->dpad_up = false; state->dpad_down = false;
    state->dpad_left = false; state->dpad_right = false;
    state->left_trigger = 0; state->right_trigger = 0;
    state->left_stick_x = 0; state->left_stick_y = 0;
    state->right_stick_x = 0; state->right_stick_y = 0;
    state->connected = false; state->updated = false;
    state->last_report_ms = 0;
}

#endif
