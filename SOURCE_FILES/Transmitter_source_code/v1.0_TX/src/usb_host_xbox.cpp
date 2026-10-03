// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * usb_host_xbox.cpp
 * 
 * ESP-IDF USB Host Library driver for Xbox GIP vendor-class controllers.
 * Handles enumeration, GIP handshake, and input report parsing.
 */

#include <Arduino.h>
#include "usb/usb_host.h"
#include "usb_host_xbox.h"
#include "gip_protocol.h"

extern HardwareSerial DebugSerial;
#define DBG DebugSerial

typedef enum {
    STATE_NOT_CONNECTED,
    STATE_DEVICE_FOUND,
    STATE_WAIT_CONFIG,
    STATE_CONFIGURING,
    STATE_WAIT_ARRIVAL,
    STATE_SEND_ACK_ARRIVAL,
    STATE_REQUEST_DESCRIPTOR,
    STATE_WAIT_DESCRIPTOR,
    STATE_SEND_POWER_ON,
    STATE_SEND_LED_ON,
    STATE_RUNNING,
    STATE_ERROR
} driver_state_t;

static gamepad_state_t   *g_gamepad       = NULL;
static usb_host_client_handle_t g_client  = NULL;
static usb_device_handle_t g_device       = NULL;
static driver_state_t     g_state         = STATE_NOT_CONNECTED;
static uint8_t            g_dev_addr      = 0;
static bool               g_device_found  = false;

static usb_transfer_t    *g_xfer_in       = NULL;
static usb_transfer_t    *g_xfer_out      = NULL;
static usb_transfer_t    *g_xfer_ctrl     = NULL;

static uint8_t g_seq_ack        = 0;
static uint8_t g_seq_descriptor = 0;
static uint8_t g_seq_power      = 0;
static uint8_t g_seq_led        = 0;

static uint8_t g_arrival_sequence = 0;

static uint8_t  g_desc_buf[512];
static uint16_t g_desc_received = 0;
static uint16_t g_desc_total    = 0;
static bool     g_desc_complete = false;

static uint32_t g_last_state_change = 0;

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
static void xfer_in_cb(usb_transfer_t *transfer);
static void xfer_out_cb(usb_transfer_t *transfer);
static void ctrl_xfer_cb(usb_transfer_t *transfer);
static void process_gip_packet(const uint8_t *data, int len);
static void send_gip_out(const uint8_t *data, int len);
static uint8_t next_seq(uint8_t *seq);
static void enter_state(driver_state_t new_state);

void usb_host_xbox_init(gamepad_state_t *state) {
    g_gamepad = state;
    gamepad_state_reset(state);

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    DBG.println("[USB] Host library installed");

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &g_client));
    DBG.println("[USB] Client registered");

    ESP_ERROR_CHECK(usb_host_transfer_alloc(DUCHESS_EP_MPS + 16, 0, &g_xfer_in));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(DUCHESS_EP_MPS + 16, 0, &g_xfer_out));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(DUCHESS_EP_MPS + 64, 0, &g_xfer_ctrl));

    enter_state(STATE_NOT_CONNECTED);
    DBG.println("[USB] Xbox host driver initialized");
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            DBG.printf("[USB] New device at address %d\n", event_msg->new_dev.address);
            g_dev_addr = event_msg->new_dev.address;
            g_device_found = true;
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            DBG.println("[USB] Device disconnected");
            if (g_device) {
                usb_host_interface_release(g_client, g_device, 0);
                usb_host_device_close(g_client, g_device);
                g_device = NULL;
            }
            if (g_gamepad) {
                g_gamepad->connected = false;
                g_gamepad->updated = true;
            }
            enter_state(STATE_NOT_CONNECTED);
            break;
        default:
            break;
    }
}

static void xfer_in_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
        process_gip_packet(transfer->data_buffer, transfer->actual_num_bytes);
    }
    if (g_state >= STATE_WAIT_ARRIVAL && g_device != NULL) {
        transfer->num_bytes = DUCHESS_EP_MPS;
        usb_host_transfer_submit(transfer);
    }
}

static void xfer_out_cb(usb_transfer_t *transfer) {
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        DBG.printf("[USB] OUT transfer status: %d\n", transfer->status);
    }
}

static void ctrl_xfer_cb(usb_transfer_t *transfer) {
    (void)transfer;
}

static void process_gip_packet(const uint8_t *data, int len) {
    if (len < 4) return;

    uint8_t command  = data[0];
    uint8_t flags    = data[1];
    uint8_t sequence = data[2];
    uint8_t length   = data[3];

    bool needs_ack  = (flags & GIP_FLAG_NEED_ACK) != 0;
    bool is_chunked = (flags & GIP_FLAG_CHUNKED) != 0;
    bool chunk_start = (flags & GIP_FLAG_CHUNK_START) != 0;

    const uint8_t *payload = data + 4;
    int payload_len = length;

    int chunk_meta_offset = 0;
    uint16_t chunk_value = 0;
    if (is_chunked) {
        if (len > 4) {
            chunk_value = data[4];
            chunk_meta_offset = 1;
            if (chunk_value & 0x80) {
                chunk_value = (chunk_value & 0x7F);
                if (len > 5) {
                    chunk_value |= ((uint16_t)data[5] << 7);
                    chunk_meta_offset = 2;
                }
            }
        }
        payload = data + 4 + chunk_meta_offset;
        payload_len = length;
    }

    switch (command) {
        case GIP_CMD_ARRIVAL: {
            DBG.printf("[GIP] Device arrival (seq=%d, len=%d)\n", sequence, length);
            if (len >= (int)sizeof(gip_arrival_t)) {
                const gip_arrival_t *arr = (const gip_arrival_t *)data;
                DBG.printf("[GIP]   VID=0x%04X PID=0x%04X\n", arr->vendor_id, arr->product_id);
            }
            g_arrival_sequence = sequence;
            if (needs_ack) {
                enter_state(STATE_SEND_ACK_ARRIVAL);
            } else {
                enter_state(STATE_REQUEST_DESCRIPTOR);
            }
            break;
        }

        case GIP_CMD_DESCRIPTOR: {
            if (chunk_start && is_chunked) {
                g_desc_total = chunk_value;
                g_desc_received = 0;
                g_desc_complete = false;
                DBG.printf("[GIP] Descriptor chunk start, total=%d\n", g_desc_total);
            }
            if (payload_len > 0 && g_desc_received + payload_len <= sizeof(g_desc_buf)) {
                memcpy(g_desc_buf + g_desc_received, payload, payload_len);
                g_desc_received += payload_len;
            }
            if (needs_ack) {
                uint8_t ack[13];
                ack[0] = GIP_CMD_ACKNOWLEDGE;
                ack[1] = GIP_FLAG_SYSTEM;
                ack[2] = sequence;
                ack[3] = 9;
                ack[4] = 0x00;
                ack[5] = GIP_CMD_DESCRIPTOR;
                ack[6] = flags & ~GIP_FLAG_NEED_ACK;
                ack[7] = g_desc_received & 0xFF;
                ack[8] = (g_desc_received >> 8) & 0xFF;
                ack[9] = 0x00;
                ack[10] = 0x00;
                uint16_t remaining = (g_desc_total > g_desc_received) ? (g_desc_total - g_desc_received) : 0;
                ack[11] = remaining & 0xFF;
                ack[12] = (remaining >> 8) & 0xFF;
                send_gip_out(ack, 13);
            }
            if (payload_len == 0 && is_chunked && !chunk_start) {
                g_desc_complete = true;
                DBG.printf("[GIP] Descriptor complete (%d bytes)\n", g_desc_received);
                enter_state(STATE_SEND_POWER_ON);
            } else if (g_desc_received >= g_desc_total && g_desc_total > 0) {
                g_desc_complete = true;
                DBG.printf("[GIP] Descriptor received (%d/%d)\n", g_desc_received, g_desc_total);
                enter_state(STATE_SEND_POWER_ON);
            }
            break;
        }

        case GIP_CMD_STATUS: {
            if (needs_ack) {
                uint8_t ack[13] = {0};
                ack[0] = GIP_CMD_ACKNOWLEDGE;
                ack[1] = GIP_FLAG_SYSTEM;
                ack[2] = sequence;
                ack[3] = 9;
                ack[5] = GIP_CMD_STATUS;
                ack[6] = flags & ~GIP_FLAG_NEED_ACK;
                ack[7] = payload_len & 0xFF;
                ack[8] = (payload_len >> 8) & 0xFF;
                send_gip_out(ack, 13);
            }
            break;
        }

        case GIP_CMD_INPUT: {
            if (len >= 18 && g_gamepad) {
                const gip_gamepad_report_t *rpt = (const gip_gamepad_report_t *)data;
                g_gamepad->a              = (rpt->buttons & GIP_BTN_A) != 0;
                g_gamepad->b              = (rpt->buttons & GIP_BTN_B) != 0;
                g_gamepad->x              = (rpt->buttons & GIP_BTN_X) != 0;
                g_gamepad->y              = (rpt->buttons & GIP_BTN_Y) != 0;
                g_gamepad->left_bumper    = (rpt->buttons & GIP_BTN_LEFT_SHOULDER) != 0;
                g_gamepad->right_bumper   = (rpt->buttons & GIP_BTN_RIGHT_SHOULDER) != 0;
                g_gamepad->left_thumb     = (rpt->buttons & GIP_BTN_LEFT_THUMB) != 0;
                g_gamepad->right_thumb    = (rpt->buttons & GIP_BTN_RIGHT_THUMB) != 0;
                g_gamepad->start          = (rpt->buttons & GIP_BTN_MENU) != 0;
                g_gamepad->back           = (rpt->buttons & GIP_BTN_VIEW) != 0;
                g_gamepad->sync           = (rpt->buttons & GIP_BTN_SYNC) != 0;
                g_gamepad->dpad_up        = (rpt->buttons & GIP_BTN_DPAD_UP) != 0;
                g_gamepad->dpad_down      = (rpt->buttons & GIP_BTN_DPAD_DOWN) != 0;
                g_gamepad->dpad_left      = (rpt->buttons & GIP_BTN_DPAD_LEFT) != 0;
                g_gamepad->dpad_right     = (rpt->buttons & GIP_BTN_DPAD_RIGHT) != 0;
                g_gamepad->left_trigger   = rpt->left_trigger;
                g_gamepad->right_trigger  = rpt->right_trigger;
                g_gamepad->left_stick_x   = rpt->left_stick_x;
                g_gamepad->left_stick_y   = rpt->left_stick_y;
                g_gamepad->right_stick_x  = rpt->right_stick_x;
                g_gamepad->right_stick_y  = rpt->right_stick_y;
                g_gamepad->connected      = true;
                g_gamepad->updated        = true;
                g_gamepad->last_report_ms = millis();
            }
            break;
        }

        case GIP_CMD_VIRTUAL_KEY: {
            if (payload_len >= 2 && g_gamepad) {
                bool pressed = (payload[0] & 0x01) != 0;
                uint8_t keycode = payload[1];
                if (keycode == GIP_KEY_GUIDE) {
                    g_gamepad->guide = pressed;
                    g_gamepad->updated = true;
                    DBG.printf("[GIP] Guide %s\n", pressed ? "PRESSED" : "RELEASED");
                }
            }
            if (needs_ack) {
                uint8_t ack[13] = {0};
                ack[0] = GIP_CMD_ACKNOWLEDGE;
                ack[1] = GIP_FLAG_SYSTEM;
                ack[2] = sequence;
                ack[3] = 9;
                ack[5] = GIP_CMD_VIRTUAL_KEY;
                ack[6] = flags & ~GIP_FLAG_NEED_ACK;
                ack[7] = payload_len & 0xFF;
                ack[8] = (payload_len >> 8) & 0xFF;
                send_gip_out(ack, 13);
            }
            break;
        }

        case GIP_CMD_ACKNOWLEDGE: {
            break;
        }

        default: {
            if (needs_ack) {
                uint8_t ack[13] = {0};
                ack[0] = GIP_CMD_ACKNOWLEDGE;
                ack[1] = GIP_FLAG_SYSTEM;
                ack[2] = sequence;
                ack[3] = 9;
                ack[5] = command;
                ack[6] = flags & ~GIP_FLAG_NEED_ACK;
                ack[7] = payload_len & 0xFF;
                ack[8] = (payload_len >> 8) & 0xFF;
                send_gip_out(ack, 13);
            }
            break;
        }
    }
}

static void send_gip_out(const uint8_t *data, int len) {
    if (!g_device || !g_xfer_out) return;
    memcpy(g_xfer_out->data_buffer, data, len);
    g_xfer_out->num_bytes = len;
    g_xfer_out->device_handle = g_device;
    g_xfer_out->bEndpointAddress = DUCHESS_EP_OUT;
    g_xfer_out->callback = xfer_out_cb;
    g_xfer_out->context = NULL;
    g_xfer_out->timeout_ms = 1000;
    esp_err_t err = usb_host_transfer_submit(g_xfer_out);
    if (err != ESP_OK) {
        DBG.printf("[USB] OUT submit failed: %s\n", esp_err_to_name(err));
    }
}

static uint8_t next_seq(uint8_t *seq) {
    (*seq)++;
    if (*seq == 0) *seq = 1;
    return *seq;
}

static void enter_state(driver_state_t new_state) {
    g_state = new_state;
    g_last_state_change = millis();
}

static bool try_open_device() {
    esp_err_t err = usb_host_device_open(g_client, g_dev_addr, &g_device);
    if (err != ESP_OK) {
        DBG.printf("[USB] Failed to open device: %s\n", esp_err_to_name(err));
        return false;
    }

    const usb_device_desc_t *dev_desc;
    err = usb_host_get_device_descriptor(g_device, &dev_desc);
    if (err != ESP_OK) {
        usb_host_device_close(g_client, g_device);
        g_device = NULL;
        return false;
    }

    DBG.printf("[USB] VID=0x%04X PID=0x%04X Class=0x%02X/0x%02X/0x%02X\n",
               dev_desc->idVendor, dev_desc->idProduct,
               dev_desc->bDeviceClass, dev_desc->bDeviceSubClass, dev_desc->bDeviceProtocol);

    bool is_xbox = (dev_desc->bDeviceClass == XBOX_IFACE_CLASS &&
                    dev_desc->bDeviceSubClass == XBOX_IFACE_SUBCLASS &&
                    dev_desc->bDeviceProtocol == XBOX_IFACE_PROTOCOL);
    bool is_duchess = (dev_desc->idVendor == DUCHESS_VID && dev_desc->idProduct == DUCHESS_PID);

    if (!is_xbox && !is_duchess) {
        DBG.println("[USB] Not an Xbox/DuchesS controller");
        usb_host_device_close(g_client, g_device);
        g_device = NULL;
        return false;
    }

    DBG.println("[USB] DuchesS controller detected!");

    const usb_config_desc_t *config_desc;
    err = usb_host_get_active_config_descriptor(g_device, &config_desc);
    if (err != ESP_OK) {
        usb_host_device_close(g_client, g_device);
        g_device = NULL;
        return false;
    }

    DBG.printf("[USB] Config: %d ifaces, maxPower=%dmA\n",
               config_desc->bNumInterfaces, config_desc->bMaxPower * 2);

    err = usb_host_interface_claim(g_client, g_device, 0, 0);
    if (err != ESP_OK) {
        DBG.printf("[USB] Failed to claim interface: %s\n", esp_err_to_name(err));
        usb_host_device_close(g_client, g_device);
        g_device = NULL;
        return false;
    }

    DBG.println("[USB] Interface 0 claimed");
    return true;
}

static void start_polling() {
    if (!g_device || !g_xfer_in) return;
    g_xfer_in->device_handle = g_device;
    g_xfer_in->bEndpointAddress = DUCHESS_EP_IN;
    g_xfer_in->callback = xfer_in_cb;
    g_xfer_in->context = NULL;
    g_xfer_in->num_bytes = DUCHESS_EP_MPS;
    g_xfer_in->timeout_ms = 100;
    esp_err_t err = usb_host_transfer_submit(g_xfer_in);
    if (err == ESP_OK) {
        DBG.println("[USB] Polling started");
    }
}

void usb_host_xbox_task(void) {
    usb_host_lib_handle_events(0, NULL);
    usb_host_client_handle_events(g_client, 0);

    uint32_t now = millis();

    switch (g_state) {
        case STATE_NOT_CONNECTED:
            if (g_device_found) {
                g_device_found = false;
                if (try_open_device()) {
                    g_seq_ack = 0; g_seq_descriptor = 0;
                    g_seq_power = 0; g_seq_led = 0;
                    g_desc_received = 0; g_desc_total = 0;
                    g_desc_complete = false;
                    start_polling();
                    enter_state(STATE_WAIT_ARRIVAL);
                    DBG.println("[USB] Waiting for GIP arrival...");
                }
            }
            break;

        case STATE_WAIT_ARRIVAL:
            if (now - g_last_state_change > 5000) {
                DBG.println("[GIP] No arrival, requesting descriptor...");
                enter_state(STATE_REQUEST_DESCRIPTOR);
            }
            break;

        case STATE_SEND_ACK_ARRIVAL: {
            uint8_t ack[13];
            ack[0] = GIP_CMD_ACKNOWLEDGE;
            ack[1] = GIP_FLAG_SYSTEM;
            ack[2] = g_arrival_sequence;
            ack[3] = 9;
            ack[4] = 0x00;
            ack[5] = GIP_CMD_ARRIVAL;
            ack[6] = GIP_FLAG_NEED_ACK | GIP_FLAG_SYSTEM;
            ack[7] = 0x1C; ack[8] = 0x00;
            ack[9] = 0x00; ack[10] = 0x00;
            ack[11] = 0x00; ack[12] = 0x00;
            send_gip_out(ack, 13);
            DBG.println("[GIP] Sent arrival ACK");
            delay(50);
            enter_state(STATE_REQUEST_DESCRIPTOR);
            break;
        }

        case STATE_REQUEST_DESCRIPTOR: {
            uint8_t desc_req[4];
            desc_req[0] = GIP_CMD_DESCRIPTOR;
            desc_req[1] = GIP_FLAG_SYSTEM;
            desc_req[2] = next_seq(&g_seq_descriptor);
            desc_req[3] = 0x00;
            send_gip_out(desc_req, 4);
            DBG.println("[GIP] Sent descriptor request");
            enter_state(STATE_WAIT_DESCRIPTOR);
            break;
        }

        case STATE_WAIT_DESCRIPTOR:
            if (g_desc_complete) {
                enter_state(STATE_SEND_POWER_ON);
            } else if (now - g_last_state_change > 5000) {
                DBG.println("[GIP] Descriptor timeout, proceeding...");
                enter_state(STATE_SEND_POWER_ON);
            }
            break;

        case STATE_SEND_POWER_ON: {
            uint8_t pwr[5];
            pwr[0] = GIP_CMD_POWER_MODE;
            pwr[1] = GIP_FLAG_SYSTEM;
            pwr[2] = next_seq(&g_seq_power);
            pwr[3] = 0x01;
            pwr[4] = GIP_PWR_ON;
            send_gip_out(pwr, 5);
            DBG.println("[GIP] Sent power-on");
            delay(100);
            enter_state(STATE_SEND_LED_ON);
            break;
        }

        case STATE_SEND_LED_ON: {
            uint8_t led[7];
            led[0] = GIP_CMD_LED_CONTROL;
            led[1] = GIP_FLAG_SYSTEM;
            led[2] = next_seq(&g_seq_led);
            led[3] = 0x03;
            led[4] = 0x00;
            led[5] = GIP_LED_ON;
            led[6] = 0x14;
            send_gip_out(led, 7);
            DBG.println("[GIP] LED on - controller ready!");
            enter_state(STATE_RUNNING);
            break;
        }

        case STATE_RUNNING:
            break;

        case STATE_ERROR:
            if (now - g_last_state_change > 3000) {
                enter_state(STATE_NOT_CONNECTED);
            }
            break;

        default:
            break;
    }
}

bool usb_host_xbox_connected(void) {
    return g_state == STATE_RUNNING && g_device != NULL;
}
