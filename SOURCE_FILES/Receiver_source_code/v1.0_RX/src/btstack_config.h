// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * btstack_config.h
 * 
 * BTStack configuration for DuchesS Wireless Receiver
 * Pico 2 W - BLE Central (HID Host) role
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// Memory allocation - BTStack needs this to build att_db_util
#define HAVE_MALLOC

// BTStack features we need
// Note: ENABLE_BLE is already defined by CMake, don't redefine here
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_GATT_CLIENT_PAIRING

// LE connection parameters
#define MAX_NR_LE_DEVICE_DB_ENTRIES    4
#define MAX_NR_HCI_CONNECTIONS         2
#define MAX_NR_L2CAP_SERVICES          4
#define MAX_NR_L2CAP_CHANNELS          4
#define MAX_NR_GATT_CLIENTS            1
#define MAX_NR_SM_LOOKUP_ENTRIES       3

// HCI ACL buffer size
#define HCI_ACL_PAYLOAD_SIZE           256
#define HCI_INCOMING_PRE_BUFFER_SIZE   14
#define HCI_OUTGOING_PRE_BUFFER_SIZE   4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT   4

// Flash storage for bonding data
#define NVM_NUM_DEVICE_DB_ENTRIES       4

// Logging
#define ENABLE_PRINTF_HEXDUMP
#define HCI_DUMP_STDOUT

#endif // BTSTACK_CONFIG_H
