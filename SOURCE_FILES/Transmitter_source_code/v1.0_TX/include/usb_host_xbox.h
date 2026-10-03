// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * usb_host_xbox.h - USB Host driver for Xbox GIP controllers
 */

#ifndef USB_HOST_XBOX_H
#define USB_HOST_XBOX_H

#include "gamepad_state.h"

void usb_host_xbox_init(gamepad_state_t *state);
void usb_host_xbox_task(void);
bool usb_host_xbox_connected(void);

#endif
