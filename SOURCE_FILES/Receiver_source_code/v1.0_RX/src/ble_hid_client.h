// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * ble_hid_client.h
 *
 * BLE Central (client) that scans for and connects to the
 * DuchesS BLE gamepad peripheral (XIAO ESP32S3 transmitter).
 *
 * Uses the BTStack GATT client (HID over GATT) to:
 *   1. Scan for BLE HID gamepads
 *   2. Connect to "DuchesS Wireless" by name
 *   3. Discover HID service and report characteristics
 *   4. Subscribe to input report notifications
 *   5. Parse HID reports into gamepad_state_t
 */

#ifndef BLE_HID_CLIENT_H
#define BLE_HID_CLIENT_H

#include "gamepad_state.h"

// Initialize BLE central and start scanning
void ble_hid_client_init(gamepad_state_t *state);

// Check if connected to the gamepad
bool ble_hid_client_connected(void);

// Clear the stored bond and restart scanning. Not called by the v1.0
// firmware (bonds are already cleared at every boot); kept for future use.
void ble_hid_client_reset_pairing(void);

// Get notification count (for debug)
uint32_t ble_hid_client_notification_count(void);

#endif // BLE_HID_CLIENT_H
