// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * gip_protocol.h
 * 
 * Xbox One / GIP (Gaming Input Protocol) definitions
 * for the Hyperkin DuchesS controller.
 *
 * VID: 0x2E24, PID: 0x0423
 * Class: 0xFF/0x47/0xD0
 * EP IN: 0x81 (64 bytes, 4ms)
 * EP OUT: 0x01 (64 bytes, 4ms)
 */

#ifndef GIP_PROTOCOL_H
#define GIP_PROTOCOL_H

#include <stdint.h>

#define DUCHESS_VID         0x2E24
#define DUCHESS_PID         0x0423
#define XBOX_IFACE_CLASS    0xFF
#define XBOX_IFACE_SUBCLASS 0x47
#define XBOX_IFACE_PROTOCOL 0xD0
#define DUCHESS_EP_IN       0x81
#define DUCHESS_EP_OUT      0x01
#define DUCHESS_EP_MPS      64

#define GIP_CMD_ACKNOWLEDGE     0x01
#define GIP_CMD_ARRIVAL         0x02
#define GIP_CMD_STATUS          0x03
#define GIP_CMD_DESCRIPTOR      0x04
#define GIP_CMD_POWER_MODE      0x05
#define GIP_CMD_AUTH            0x06
#define GIP_CMD_VIRTUAL_KEY     0x07
#define GIP_CMD_AUDIO_CONTROL   0x08
#define GIP_CMD_LED_CONTROL     0x0A
#define GIP_CMD_HID_REPORT      0x0B
#define GIP_CMD_FIRMWARE        0x0C
#define GIP_CMD_SERIAL_NUMBER   0x1E
#define GIP_CMD_AUDIO_SAMPLES   0x60
#define GIP_CMD_INPUT           0x20

#define GIP_FLAG_NEED_ACK       0x10
#define GIP_FLAG_SYSTEM         0x20
#define GIP_FLAG_CHUNK_START    0x40
#define GIP_FLAG_CHUNKED        0x80

#define GIP_PWR_ON              0x00
#define GIP_PWR_SLEEP           0x01
#define GIP_PWR_OFF             0x04
#define GIP_PWR_RESET           0x07

#define GIP_LED_OFF             0x00
#define GIP_LED_ON              0x01
#define GIP_LED_BLINK_FAST      0x02
#define GIP_LED_BLINK_NORMAL    0x03
#define GIP_LED_BLINK_SLOW      0x04
#define GIP_LED_FADE_SLOW       0x08
#define GIP_LED_FADE_FAST       0x09

#pragma pack(push, 1)

typedef struct {
    uint8_t command;
    uint8_t flags;
    uint8_t sequence;
    uint8_t length;
} gip_header_t;

typedef struct {
    uint8_t unk1;
    uint8_t inner_command;
    uint8_t inner_flags;
    uint16_t bytes_received;
    uint16_t unk2;
    uint16_t remaining_buffer;
} gip_ack_payload_t;

typedef struct {
    gip_header_t header;
    gip_ack_payload_t payload;
} gip_ack_packet_t;

typedef struct {
    gip_header_t header;
    uint8_t subcommand;
} gip_power_packet_t;

typedef struct {
    gip_header_t header;
    uint8_t unk;
    uint8_t mode;
    uint8_t brightness;
} gip_led_packet_t;

typedef struct {
    gip_header_t header;
    uint16_t buttons;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t  left_stick_x;
    int16_t  left_stick_y;
    int16_t  right_stick_x;
    int16_t  right_stick_y;
} gip_gamepad_report_t;

typedef struct {
    gip_header_t header;
    uint8_t serial[8];
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t fw_major;
    uint16_t fw_minor;
    uint16_t fw_build;
    uint16_t fw_revision;
    uint16_t hw_major;
    uint16_t hw_minor;
    uint16_t hw_build;
    uint16_t hw_revision;
} gip_arrival_t;

typedef struct {
    gip_header_t header;
    uint8_t pressed;
    uint8_t keycode;
} gip_keystroke_t;

#pragma pack(pop)

#define GIP_BTN_SYNC            0x0001
#define GIP_BTN_MENU            0x0004
#define GIP_BTN_VIEW            0x0008
#define GIP_BTN_A               0x0010
#define GIP_BTN_B               0x0020
#define GIP_BTN_X               0x0040
#define GIP_BTN_Y               0x0080
#define GIP_BTN_DPAD_UP         0x0100
#define GIP_BTN_DPAD_DOWN       0x0200
#define GIP_BTN_DPAD_LEFT       0x0400
#define GIP_BTN_DPAD_RIGHT      0x0800
#define GIP_BTN_LEFT_SHOULDER   0x1000
#define GIP_BTN_RIGHT_SHOULDER  0x2000
#define GIP_BTN_LEFT_THUMB      0x4000
#define GIP_BTN_RIGHT_THUMB     0x8000

#define GIP_KEY_GUIDE           0x5B

#endif
