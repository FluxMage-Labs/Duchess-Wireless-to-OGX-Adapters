// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * ble_output.cpp
 *
 * BLE HID Gamepad output using ESP32-BLE-Gamepad library.
 * Advertises as "DuchesS Wireless" — the Pi Pico 2 W receiver
 * scans for exactly this name and auto-connects.
 *
 * Report format produced (15 bytes, no report ID prefix), in the order
 * ESP32-BLE-Gamepad's sendReport() writes it (axes first, hat LAST):
 *   [0-1]   Buttons (16 bits, little-endian)
 *   [2-3]   X axis  (int16 LE) = left stick X
 *   [4-5]   Y axis  (int16 LE) = left stick Y
 *   [6-7]   Z axis  (int16 LE) = right stick X
 *   [8-9]   Rz axis (int16 LE) = right stick Y
 *   [10-11] Rx axis (int16 LE) = left trigger (mapped -32767..32767)
 *   [12-13] Ry axis (int16 LE) = right trigger (mapped -32767..32767)
 *   [14]    Hat switch (0=centered, 1-8 clockwise from N)
 *
 * Button bit assignments (must match receiver's parse_hid_report):
 *   0=A, 1=B, 2=X, 3=Y, 4=LB, 5=RB, 6=Back, 7=Start,
 *   8=L3, 9=R3, 10=Guide, 11=Sync
 */

#include <Arduino.h>
#include <BleGamepad.h>
#include "ble_output.h"
#include "gamepad_state.h"

extern HardwareSerial DebugSerial;
#define DBG DebugSerial

// BLE Gamepad — name MUST be "DuchesS Wireless" to match receiver.
// The manufacturer string ("Hypk") is informational only.
static BleGamepad bleGamepad("DuchesS Wireless", "Hypk", 100);
static BleGamepadConfiguration bleConfig;
static bool g_ble_init = false;

void ble_output_init(void) {
    DBG.println("[BLE] Initializing BLE Gamepad...");

    // Clear any stale bonds from previous pairing sessions.
    // When the Pico is reflashed its bond data is lost, but the ESP32's
    // NVS keeps the old bond, causing silent encryption mismatches.
    NimBLEDevice::init("");
    int bondCount = NimBLEDevice::getNumBonds();
    if (bondCount > 0) {
        DBG.printf("[BLE] Clearing %d stale bond(s)\n", bondCount);
        NimBLEDevice::deleteAllBonds();
    }
    NimBLEDevice::deinit(false);

    bleConfig.setAutoReport(false);
    bleConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);
    bleConfig.setButtonCount(16);
    bleConfig.setHatSwitchCount(1);

    // Axes: X, Y = left stick; Z, Rz = right stick; Rx, Ry = triggers
    bleConfig.setIncludeXAxis(true);
    bleConfig.setIncludeYAxis(true);
    bleConfig.setIncludeZAxis(true);
    bleConfig.setIncludeRzAxis(true);
    bleConfig.setIncludeRxAxis(true);
    bleConfig.setIncludeRyAxis(true);
    bleConfig.setIncludeSlider1(false);
    bleConfig.setIncludeSlider2(false);

    // 16-bit signed axis range
    bleConfig.setAxesMin(-32767);
    bleConfig.setAxesMax(32767);

    // Generic identity (receiver doesn't care about VID/PID, only the name)
    // Left unchanged from the verified-working build.
    bleConfig.setVid(0x2E24);   // Hyperkin
    bleConfig.setPid(0x0BEE);   // Custom

    bleGamepad.begin(&bleConfig);
    g_ble_init = true;

    DBG.println("[BLE] Advertising as 'DuchesS Wireless'");
    DBG.println("[BLE] Pico 2 W receiver will auto-connect to this name");
}

void ble_output_send(const gamepad_state_t *state) {
    if (!g_ble_init || !bleGamepad.isConnected()) return;

    // --- Buttons ---
    // Map to BUTTON_1..BUTTON_12 matching receiver's bit order
    if (state->a)            bleGamepad.press(BUTTON_1);  else bleGamepad.release(BUTTON_1);
    if (state->b)            bleGamepad.press(BUTTON_2);  else bleGamepad.release(BUTTON_2);
    if (state->x)            bleGamepad.press(BUTTON_3);  else bleGamepad.release(BUTTON_3);
    if (state->y)            bleGamepad.press(BUTTON_4);  else bleGamepad.release(BUTTON_4);
    if (state->left_bumper)  bleGamepad.press(BUTTON_5);  else bleGamepad.release(BUTTON_5);
    if (state->right_bumper) bleGamepad.press(BUTTON_6);  else bleGamepad.release(BUTTON_6);
    if (state->back)         bleGamepad.press(BUTTON_7);  else bleGamepad.release(BUTTON_7);
    if (state->start)        bleGamepad.press(BUTTON_8);  else bleGamepad.release(BUTTON_8);
    if (state->left_thumb)   bleGamepad.press(BUTTON_9);  else bleGamepad.release(BUTTON_9);
    if (state->right_thumb)  bleGamepad.press(BUTTON_10); else bleGamepad.release(BUTTON_10);
    if (state->guide)        bleGamepad.press(BUTTON_11); else bleGamepad.release(BUTTON_11);
    if (state->sync)         bleGamepad.press(BUTTON_12); else bleGamepad.release(BUTTON_12);

    // --- D-Pad as Hat Switch ---
    // Values must match receiver's parser:
    //   0=centered, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
    uint8_t hat = HAT_CENTERED;
    if      (state->dpad_up && state->dpad_right)   hat = HAT_UP_RIGHT;
    else if (state->dpad_down && state->dpad_right)  hat = HAT_DOWN_RIGHT;
    else if (state->dpad_down && state->dpad_left)   hat = HAT_DOWN_LEFT;
    else if (state->dpad_up && state->dpad_left)     hat = HAT_UP_LEFT;
    else if (state->dpad_up)                         hat = HAT_UP;
    else if (state->dpad_right)                      hat = HAT_RIGHT;
    else if (state->dpad_down)                       hat = HAT_DOWN;
    else if (state->dpad_left)                       hat = HAT_LEFT;
    bleGamepad.setHat1(hat);

    // --- Sticks ---
    // GIP gives signed 16-bit (-32768..32767), BLE range is -32767..32767
    bleGamepad.setX(state->left_stick_x);
    bleGamepad.setY(state->left_stick_y);
    bleGamepad.setZ(state->right_stick_x);
    bleGamepad.setRZ(state->right_stick_y);

    // --- Triggers ---
    // GIP gives 0-1023, map to -32767..32767 for the BLE axis range
    // Receiver reverses this: (raw + 32767) * 1023 / 65534
    int16_t lt_mapped = (int16_t)((int32_t)state->left_trigger  * 65534 / 1023 - 32767);
    int16_t rt_mapped = (int16_t)((int32_t)state->right_trigger * 65534 / 1023 - 32767);
    bleGamepad.setRX(lt_mapped);
    bleGamepad.setRY(rt_mapped);

    bleGamepad.sendReport();
}

bool ble_output_connected(void) {
    return g_ble_init && bleGamepad.isConnected();
}
