// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * tusb_config.h
 *
 * TinyUSB configuration for OG Xbox XID controller emulation.
 * The Pico 2 W presents itself as an OG Xbox Controller S
 * (VID 0x045E, PID 0x0289) with XID vendor-class interface.
 *
 * We use a CUSTOM class driver (not the built-in vendor class)
 * because XID requires interrupt endpoints, and TinyUSB's vendor
 * class internally uses bulk endpoints.
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// Board/MCU config
#define CFG_TUSB_MCU               OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE      OPT_MODE_DEVICE
#define CFG_TUSB_OS                OPT_OS_PICO

// USB device configuration
#define CFG_TUD_ENDPOINT0_SIZE     8

// Disable ALL built-in classes - we use a custom XID class driver
// registered via usbd_app_driver_get_cb()
#define CFG_TUD_HID               0
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

#endif // TUSB_CONFIG_H
