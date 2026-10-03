// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * ble_output.h - BLE HID gamepad output via ESP32-BLE-Gamepad
 */

#ifndef BLE_OUTPUT_H
#define BLE_OUTPUT_H

#include "gamepad_state.h"

void ble_output_init(void);
void ble_output_send(const gamepad_state_t *state);
bool ble_output_connected(void);

#endif
