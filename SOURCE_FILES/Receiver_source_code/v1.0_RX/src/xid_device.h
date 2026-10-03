// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * xid_device.h
 *
 * OG Xbox XID (Xbox Input Device) USB device emulation.
 * Presents the Pico 2 W as an Original Xbox Controller S
 * to the Xbox console via a USB-to-Xbox adapter cable.
 *
 * XID protocol key facts:
 *   - Interface class 0x58 ('X'), subclass 0x42 ('B')
 *   - NOT standard HID - uses vendor-specific control transfers
 *   - VID 0x045E (Microsoft), PID 0x0289 (Controller S)
 *   - 20-byte input report, 6-byte output report (rumble)
 *   - USB 1.1 Full Speed, 4ms polling interval
 */

#ifndef XID_DEVICE_H
#define XID_DEVICE_H

#include "gamepad_state.h"

// Initialize the USB XID device (TinyUSB)
void xid_device_init(gamepad_state_t *state);

// Call from main loop to process USB tasks
void xid_device_task(void);

// Update the XID input report from gamepad state
// Called when new BLE data arrives
void xid_device_update(void);

// Send current report to Xbox via interrupt IN endpoint
// Call every 4ms to match Xbox polling interval
void xid_device_send_report(void);

#endif // XID_DEVICE_H
