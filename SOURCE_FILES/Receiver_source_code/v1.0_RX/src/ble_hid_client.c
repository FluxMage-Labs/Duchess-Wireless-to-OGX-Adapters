// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * ble_hid_client.c
 *
 * BLE Central (HID Host) using BTStack on Pico 2 W.
 *
 * Scans for a BLE HID gamepad named "DuchesS Wireless",
 * connects, discovers HID service, subscribes to input
 * report notifications, and parses them into gamepad_state_t.
 *
 * The transmitter (XIAO ESP32S3) uses ESP32-BLE-Gamepad which
 * sends standard BLE HID gamepad reports with:
 *   - 16 buttons (1 bit each)
 *   - 1 hat switch (4 bits)
 *   - 6 axes: X, Y, Z, Rz, Rx, Ry (each 16-bit signed)
 *
 * GATT flow (following hog_host_demo.c pattern):
 *   1. Connection complete  -> MTU negotiation
 *   2. GATT_EVENT_MTU       -> discover HID service (0x1812)
 *   3. SERVICE_QUERY_RESULT -> save service handles
 *   4. QUERY_COMPLETE       -> discover characteristics in service
 *   5. CHAR_QUERY_RESULT    -> save notifiable Report char (0x2A4D)
 *   6. QUERY_COMPLETE       -> subscribe to notifications (write CCCD)
 *   7. QUERY_COMPLETE       -> ready, receive GATT_EVENT_NOTIFICATION
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "ble/le_device_db.h"
#include "ble_hid_client.h"
#include "gamepad_state.h"

// ---------------------------------------------------------------------------
// Target device name to look for
// ---------------------------------------------------------------------------
#define TARGET_NAME "DuchesS Wireless"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static gamepad_state_t *g_gamepad = NULL;

typedef enum {
    BLE_STATE_IDLE,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_PAIRING,
    BLE_STATE_DISCOVERING_SERVICES,
    BLE_STATE_DISCOVERING_CHARS,
    BLE_STATE_SUBSCRIBING,
    BLE_STATE_READY,
    BLE_STATE_DISCONNECTED
} ble_state_t;

static ble_state_t g_ble_state = BLE_STATE_IDLE;
static hci_con_handle_t g_conn_handle = HCI_CON_HANDLE_INVALID;
static bd_addr_t g_target_addr;
static bd_addr_type_t g_target_addr_type;
static bool g_target_found = false;

// GATT discovery state
static gatt_client_service_t g_hid_service;
static gatt_client_characteristic_t g_report_char;
static bool g_hid_service_found = false;
static bool g_report_char_found = false;
static gatt_client_notification_t g_notification_listener;

// Debug counter
static uint32_t g_notification_count = 0;

// Reconnection
static bool g_has_bonded_addr = false;
static bd_addr_t g_bonded_addr;
static bd_addr_type_t g_bonded_addr_type;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void start_scanning(void);
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void parse_hid_report(const uint8_t *data, uint16_t len);

// ---------------------------------------------------------------------------
// Initialize BLE
// ---------------------------------------------------------------------------
void ble_hid_client_init(gamepad_state_t *state) {
    g_gamepad = state;
    gamepad_state_reset(state);

    printf("[BLE] Initializing BLE HID client...\n");

    // Initialize L2CAP - required for ATT/GATT to function
    l2cap_init();

    // Initialize GATT client - required for all GATT operations
    gatt_client_init();

    // Setup SM (Security Manager) for Just Works pairing
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);

    // Clear any stale bonds from previous sessions.
    // The XIAO transmitter also clears bonds on boot — both sides must
    // start fresh to avoid encrypted link mismatches after reflashing.
    int bond_count = le_device_db_count();
    if (bond_count > 0) {
        printf("[BLE] Clearing %d stale bond(s)\n", bond_count);
        for (int i = bond_count - 1; i >= 0; i--) {
            le_device_db_remove(i);
        }
    }

    // Register HCI event handler
    static btstack_packet_callback_registration_t hci_event_callback;
    hci_event_callback.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback);

    // NOTE: Do NOT call gatt_client_listen_for_characteristic_value_updates here.
    // It must be called after connection with the real con_handle, or it corrupts
    // the GATT client state. We register it in the connection complete handler.

    // Power on
    hci_power_control(HCI_POWER_ON);

    printf("[BLE] BLE stack initialized, waiting for power on...\n");
}

bool ble_hid_client_connected(void) {
    return g_ble_state == BLE_STATE_READY;
}

uint32_t ble_hid_client_notification_count(void) {
    return g_notification_count;
}

void ble_hid_client_reset_pairing(void) {
    printf("[BLE] Clearing bonding data and restarting scan...\n");
    g_has_bonded_addr = false;
    memset(g_bonded_addr, 0, sizeof(g_bonded_addr));

    // Disconnect if connected
    if (g_conn_handle != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(g_conn_handle);
    }

    // Delete all bonding data
    gap_delete_bonding(g_bonded_addr_type, g_bonded_addr);

    g_ble_state = BLE_STATE_IDLE;
    start_scanning();
}

// ---------------------------------------------------------------------------
// Start BLE scanning
// ---------------------------------------------------------------------------
static void start_scanning(void) {
    printf("[BLE] Starting BLE scan for '%s'...\n", TARGET_NAME);
    fflush(stdout);
    g_target_found = false;
    g_ble_state = BLE_STATE_SCANNING;

    // Active scan to get scan response (which contains the device name)
    gap_set_scan_params(1, 0x0030, 0x0030, 0);
    gap_start_scan();
}

// ---------------------------------------------------------------------------
// Connect to discovered target
// ---------------------------------------------------------------------------
static void connect_to_target(void) {
    printf("[BLE] Connecting to %02X:%02X:%02X:%02X:%02X:%02X (type %d)...\n",
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5],
           g_target_addr_type);

    g_ble_state = BLE_STATE_CONNECTING;
    gap_stop_scan();
    gap_connect(g_target_addr, g_target_addr_type);
}

// ---------------------------------------------------------------------------
// Main HCI packet handler
// ---------------------------------------------------------------------------
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case BTSTACK_EVENT_STATE: {
            uint8_t state = btstack_event_state_get_state(packet);
            if (state == HCI_STATE_WORKING) {
                printf("[BLE] BTStack ready, starting scan...\n");
                start_scanning();
            }
            break;
        }

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (g_ble_state != BLE_STATE_SCANNING) break;

            // Get address of advertising device
            bd_addr_t addr;
            gap_event_advertising_report_get_address(packet, addr);

            // Get advertised name
            uint8_t adv_data_len = gap_event_advertising_report_get_data_length(packet);
            const uint8_t *adv_data = gap_event_advertising_report_get_data(packet);

            // Look for the complete or shortened local name
            ad_context_t context;
            char name[32] = {0};
            for (ad_iterator_init(&context, adv_data_len, adv_data);
                 ad_iterator_has_more(&context);
                 ad_iterator_next(&context)) {

                uint8_t type = ad_iterator_get_data_type(&context);
                uint8_t len = ad_iterator_get_data_len(&context);
                const uint8_t *data = ad_iterator_get_data(&context);

                if ((type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
                     type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) && len < sizeof(name)) {
                    memcpy(name, data, len);
                    name[len] = '\0';
                }
            }

            // Check if this is our target
            if (strlen(name) > 0 && strncmp(name, TARGET_NAME, strlen(TARGET_NAME)) == 0) {
                memcpy(g_target_addr, addr, sizeof(bd_addr_t));
                g_target_addr_type = gap_event_advertising_report_get_address_type(packet);
                g_target_found = true;

                printf("[BLE] *** Found target: '%s' ***\n", name);
                fflush(stdout);
                connect_to_target();
            }
            break;
        }

        case HCI_EVENT_LE_META: {
            uint8_t sub_event = hci_event_le_meta_get_subevent_code(packet);
            if (sub_event == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
                if (status == 0) {
                    g_conn_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    g_ble_state = BLE_STATE_CONNECTED;

                    printf("[BLE] Connected! Handle=0x%04X\n", g_conn_handle);

                    // Save bonded address for reconnection
                    memcpy(g_bonded_addr, g_target_addr, sizeof(bd_addr_t));
                    g_bonded_addr_type = g_target_addr_type;
                    g_has_bonded_addr = true;

                    // Start GATT discovery immediately
                    printf("[BLE] Discovering HID service (UUID 0x1812)...\n");
                    g_ble_state = BLE_STATE_DISCOVERING_SERVICES;
                    g_hid_service_found = false;

                    uint8_t disc_err = gatt_client_discover_primary_services_by_uuid16(
                        &handle_gatt_client_event, g_conn_handle, 0x1812);
                    if (disc_err != ERROR_CODE_SUCCESS) {
                        printf("[BLE] Service discovery failed to start: 0x%02X\n", disc_err);
                        gap_disconnect(g_conn_handle);
                    }
                } else {
                    printf("[BLE] Connection failed: %d\n", status);
                    g_ble_state = BLE_STATE_IDLE;
                    start_scanning();
                }
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            printf("[BLE] Disconnected!\n");
            g_conn_handle = HCI_CON_HANDLE_INVALID;
            g_ble_state = BLE_STATE_DISCONNECTED;

            if (g_gamepad) {
                g_gamepad->connected = false;
                g_gamepad->updated = true;
            }

            printf("[BLE] Restarting scan...\n");
            start_scanning();
            break;
        }

        case SM_EVENT_JUST_WORKS_REQUEST:
            printf("[BLE] Just Works pairing requested - accepting\n");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            uint8_t status = sm_event_pairing_complete_get_status(packet);
            printf("[BLE] Pairing complete: status=%d\n", status);
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Unified GATT client event handler
//
// Handles all GATT events in sequence: service discovery ->
// characteristic discovery -> subscribe -> notifications.
//
// Using a single handler (as in hog_host_demo.c) is required because
// BTStack's GATT client dispatches all events through the callback
// registered with each operation, and the state machine needs to be
// contiguous. Using multiple separate callbacks per stage breaks
// event dispatch in threadsafe_background mode.
// ---------------------------------------------------------------------------
static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event = hci_event_packet_get_type(packet);
    uint8_t err;

    switch (event) {

        // ----------------------------------------------------------------
        // Service discovered
        // ----------------------------------------------------------------
        case GATT_EVENT_SERVICE_QUERY_RESULT:
            gatt_event_service_query_result_get_service(packet, &g_hid_service);
            g_hid_service_found = true;
            printf("[BLE] Found HID service (handles 0x%04X-0x%04X)\n",
                   g_hid_service.start_group_handle, g_hid_service.end_group_handle);
            break;

        // ----------------------------------------------------------------
        // Characteristic discovered
        // ----------------------------------------------------------------
        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
            gatt_client_characteristic_t ch;
            gatt_event_characteristic_query_result_get_characteristic(packet, &ch);

            // HID Report characteristic UUID = 0x2A4D, must have Notify property
            if (ch.uuid16 == 0x2A4D && (ch.properties & ATT_PROPERTY_NOTIFY)) {
                g_report_char = ch;
                g_report_char_found = true;
                printf("[BLE] -> This is our Input Report char!\n");
            }
            break;
        }

        // ----------------------------------------------------------------
        // Notification received
        // ----------------------------------------------------------------
        case GATT_EVENT_NOTIFICATION: {
            uint16_t value_len = gatt_event_notification_get_value_length(packet);
            const uint8_t *value = gatt_event_notification_get_value(packet);
            g_notification_count++;
            if (g_notification_count <= 3 || (g_notification_count % 500) == 0) {
                printf("[BLE] Notification #%lu (%d bytes)\n",
                       (unsigned long)g_notification_count, value_len);
            }
            parse_hid_report(value, value_len);
            break;
        }

        // ----------------------------------------------------------------
        // Query complete - advance state machine to next step
        // ----------------------------------------------------------------
        case GATT_EVENT_QUERY_COMPLETE: {
            uint8_t att_status = gatt_event_query_complete_get_att_status(packet);

            if (att_status != ATT_ERROR_SUCCESS) {
                printf("[BLE] GATT query failed (state=%d att_status=0x%02X)\n",
                       g_ble_state, att_status);
                gap_disconnect(g_conn_handle);
                break;
            }

            switch (g_ble_state) {

                case BLE_STATE_DISCOVERING_SERVICES:
                    if (g_hid_service_found) {
                        printf("[BLE] Discovering HID report characteristics...\n");
                        g_ble_state = BLE_STATE_DISCOVERING_CHARS;
                        g_report_char_found = false;
                        err = gatt_client_discover_characteristics_for_service(
                            &handle_gatt_client_event, g_conn_handle, &g_hid_service);
                        if (err != ERROR_CODE_SUCCESS) {
                            printf("[BLE] Char discovery failed to start: 0x%02X\n", err);
                            gap_disconnect(g_conn_handle);
                        }
                    } else {
                        printf("[BLE] HID service NOT found! Disconnecting...\n");
                        gap_disconnect(g_conn_handle);
                    }
                    break;

                case BLE_STATE_DISCOVERING_CHARS:
                    if (g_report_char_found) {
                        // Register notification listener for this specific characteristic
                        printf("[BLE] Registering notification listener for value_handle=0x%04X\n",
                               g_report_char.value_handle);
                        gatt_client_listen_for_characteristic_value_updates(
                            &g_notification_listener, &handle_gatt_client_event,
                            g_conn_handle, &g_report_char);

                        printf("[BLE] Subscribing to input report notifications...\n");
                        g_ble_state = BLE_STATE_SUBSCRIBING;
                        err = gatt_client_write_client_characteristic_configuration(
                            &handle_gatt_client_event, g_conn_handle,
                            &g_report_char, GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
                        if (err != ERROR_CODE_SUCCESS) {
                            printf("[BLE] Subscribe failed to start: 0x%02X\n", err);
                            gap_disconnect(g_conn_handle);
                        }
                    } else {
                        printf("[BLE] No notifiable HID Report char found!\n");
                        gap_disconnect(g_conn_handle);
                    }
                    break;

                case BLE_STATE_SUBSCRIBING:
                    printf("[BLE] *** Subscribed! Ready to receive gamepad data ***\n");
                    g_ble_state = BLE_STATE_READY;
                    if (g_gamepad) {
                        g_gamepad->connected = true;
                        g_gamepad->updated = true;
                    }
                    break;

                default:
                    printf("[BLE] QUERY_COMPLETE in unexpected state %d\n", g_ble_state);
                    break;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Parse ESP32-BLE-Gamepad HID report into gamepad_state_t
//
// ESP32-BLE-Gamepad with our config sends (no report ID prefix):
//   Buttons:  2 bytes (16 buttons, bit-packed)
//   X axis:   2 bytes (int16, left stick X)
//   Y axis:   2 bytes (int16, left stick Y)
//   Z axis:   2 bytes (int16, right stick X)
//   Rz axis:  2 bytes (int16, right stick Y)
//   Rx axis:  2 bytes (int16, left trigger, mapped)
//   Ry axis:  2 bytes (int16, right trigger, mapped)
//   Hat:      1 byte  (0-8, 0=centered)
// Total: 15 bytes
// NOTE: Hat switch comes AFTER axes in ESP32-BLE-Gamepad's sendReport()
//
// Button mapping from transmitter:
//   1=A, 2=B, 3=X, 4=Y, 5=LB, 6=RB, 7=Back, 8=Start,
//   9=L3, 10=R3, 11=Guide, 12=Sync
// ---------------------------------------------------------------------------
static void parse_hid_report(const uint8_t *data, uint16_t len) {
    if (!g_gamepad || len < 15) return;

    // Buttons (2 bytes, little-endian, 16 bits)
    uint16_t buttons = data[0] | (data[1] << 8);

    g_gamepad->a            = (buttons & (1 << 0)) != 0;
    g_gamepad->b            = (buttons & (1 << 1)) != 0;
    g_gamepad->x            = (buttons & (1 << 2)) != 0;
    g_gamepad->y            = (buttons & (1 << 3)) != 0;
    g_gamepad->left_bumper  = (buttons & (1 << 4)) != 0;
    g_gamepad->right_bumper = (buttons & (1 << 5)) != 0;
    g_gamepad->back         = (buttons & (1 << 6)) != 0;
    g_gamepad->start        = (buttons & (1 << 7)) != 0;
    g_gamepad->left_thumb   = (buttons & (1 << 8)) != 0;
    g_gamepad->right_thumb  = (buttons & (1 << 9)) != 0;
    g_gamepad->guide        = (buttons & (1 << 10)) != 0;

    // Axes (each 2 bytes, little-endian, signed 16-bit)
    // Note: axes come right after buttons, hat switch is at the END
    g_gamepad->left_stick_x  = (int16_t)(data[2] | (data[3] << 8));
    g_gamepad->left_stick_y  = (int16_t)(data[4] | (data[5] << 8));
    g_gamepad->right_stick_x = (int16_t)(data[6] | (data[7] << 8));
    g_gamepad->right_stick_y = (int16_t)(data[8] | (data[9] << 8));

    // Triggers (sent as int16 mapped -32767..32767, convert back to 0-1023)
    int16_t lt_raw = (int16_t)(data[10] | (data[11] << 8));
    int16_t rt_raw = (int16_t)(data[12] | (data[13] << 8));

    // Hat switch (1 byte, at end of report)
    uint8_t hat = data[14];
    g_gamepad->dpad_up    = (hat == 1 || hat == 2 || hat == 8);
    g_gamepad->dpad_right = (hat == 2 || hat == 3 || hat == 4);
    g_gamepad->dpad_down  = (hat == 4 || hat == 5 || hat == 6);
    g_gamepad->dpad_left  = (hat == 6 || hat == 7 || hat == 8);
    // Map from -32767..32767 back to 0..1023
    g_gamepad->left_trigger  = (uint16_t)(((int32_t)lt_raw + 32767) * 1023 / 65534);
    g_gamepad->right_trigger = (uint16_t)(((int32_t)rt_raw + 32767) * 1023 / 65534);

    g_gamepad->connected = true;
    g_gamepad->updated = true;
    g_gamepad->last_report_ms = to_ms_since_boot(get_absolute_time());
}
