// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * gamepad_state.h
 *
 * Shared gamepad state for the DuchesS receiver.
 * BLE client writes this, USB XID device reads it.
 */

#ifndef GAMEPAD_STATE_H
#define GAMEPAD_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Sticks (signed 16-bit, -32768 to 32767)
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;

    // Triggers (0-1023 from BLE, mapped to 0-255 for XID)
    uint16_t left_trigger;
    uint16_t right_trigger;

    // Digital buttons
    bool a, b, x, y;
    bool left_bumper, right_bumper;
    bool left_thumb, right_thumb;
    bool start, back;
    bool guide;
    bool dpad_up, dpad_down, dpad_left, dpad_right;

    // Status
    bool connected;
    bool updated;
    uint32_t last_report_ms;
} gamepad_state_t;

void gamepad_state_reset(gamepad_state_t *state);

#endif // GAMEPAD_STATE_H
