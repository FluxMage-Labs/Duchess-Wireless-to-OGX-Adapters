// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * xid_device.c
 *
 * OG Xbox XID USB device emulation using a CUSTOM TinyUSB class driver.
 *
 * Why custom? TinyUSB's built-in vendor class driver uses BULK endpoints,
 * but XID requires INTERRUPT endpoints. The Xbox polls interrupt IN at
 * 4ms intervals and sends rumble on interrupt OUT. Using bulk endpoints
 * means the Xbox never sees a valid controller.
 *
 * This driver registers via usbd_app_driver_get_cb() and:
 *   - Claims interface class 0x58/0x42 (Xbox/Controller)
 *   - Opens EP IN and EP OUT as INTERRUPT endpoints
 *   - Handles XID vendor control transfers (descriptor, capabilities, reports)
 *   - Keeps a transfer pending on EP IN so data is ready when Xbox polls
 *   - Accepts rumble on EP OUT (ignored — can't rumble wirelessly)
 */

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "xid_device.h"
#include "gamepad_state.h"

// ---------------------------------------------------------------------------
// XID descriptor (16 bytes) - Controller S identity
// ---------------------------------------------------------------------------
static const uint8_t xid_descriptor[] = {
    0x10,       // bLength (16)
    0x42,       // bDescriptorType (XID)
    0x00, 0x01, // bcdXid (1.0)
    0x01,       // bType (gamepad)
    0x02,       // bSubType (Controller S)
    0x14,       // bMaxInputReportSize (20)
    0x06,       // bMaxOutputReportSize (6)
    0xFF, 0xFF, // wAlternateProductId[0]
    0xFF, 0xFF, // wAlternateProductId[1]
    0xFF, 0xFF, // wAlternateProductId[2]
    0xFF, 0xFF, // wAlternateProductId[3]
};

// ---------------------------------------------------------------------------
// Input capabilities (20 bytes) - which fields are valid
// ---------------------------------------------------------------------------
static const uint8_t xid_input_caps[] = {
    0x00,       // Report ID
    0x14,       // Length
    0xFF,       // Digital buttons
    0x00,       // Reserved
    0xFF, 0xFF, 0xFF, 0xFF, // A, B, X, Y
    0xFF, 0xFF, // Black, White
    0xFF, 0xFF, // Triggers
    0xFF, 0xFF, 0xFF, 0xFF, // Left stick
    0xFF, 0xFF, 0xFF, 0xFF, // Right stick
};

// ---------------------------------------------------------------------------
// Output capabilities (6 bytes) - rumble
// ---------------------------------------------------------------------------
static const uint8_t xid_output_caps[] = {
    0x00,       // Report ID
    0x06,       // Length
    0xFF, 0xFF, // Left motor
    0xFF, 0xFF, // Right motor
};

// ---------------------------------------------------------------------------
// USB Device Descriptor
// ---------------------------------------------------------------------------
static const tusb_desc_device_t xid_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0110,   // USB 1.1
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 8,
    .idVendor           = 0x045E,   // Microsoft
    .idProduct          = 0x0289,   // Controller S
    .bcdDevice          = 0x0121,   // Device version 1.21
    .iManufacturer      = 0,
    .iProduct           = 0,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1,
};

// ---------------------------------------------------------------------------
// Configuration descriptor with XID interface + interrupt endpoints
// Total: 9 (config) + 9 (interface) + 7 (EP IN) + 7 (EP OUT) = 32 bytes
// ---------------------------------------------------------------------------
#define XID_EP_IN   0x81
#define XID_EP_OUT  0x02
#define XID_EP_SIZE 32
#define XID_INTERVAL 4  // 4ms polling

static const uint8_t xid_config_desc[] = {
    // Configuration descriptor
    9,                              // bLength
    TUSB_DESC_CONFIGURATION,        // bDescriptorType
    32, 0,                          // wTotalLength (32 bytes)
    1,                              // bNumInterfaces
    1,                              // bConfigurationValue
    0,                              // iConfiguration
    0x80,                           // bmAttributes (bus powered)
    0xFA,                           // bMaxPower (500mA)

    // Interface descriptor - XID
    9,                              // bLength
    TUSB_DESC_INTERFACE,            // bDescriptorType
    0,                              // bInterfaceNumber
    0,                              // bAlternateSetting
    2,                              // bNumEndpoints
    0x58,                           // bInterfaceClass ('X' = Xbox)
    0x42,                           // bInterfaceSubClass ('B' = Controller)
    0x00,                           // bInterfaceProtocol
    0,                              // iInterface

    // Endpoint IN (device to host - input reports)
    7,                              // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    XID_EP_IN,                      // bEndpointAddress (0x81)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    XID_EP_SIZE, 0,                 // wMaxPacketSize
    XID_INTERVAL,                   // bInterval (4ms)

    // Endpoint OUT (host to device - rumble)
    7,                              // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    XID_EP_OUT,                     // bEndpointAddress (0x02)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    XID_EP_SIZE, 0,                 // wMaxPacketSize
    XID_INTERVAL,                   // bInterval (4ms)
};

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------
static uint8_t xid_input_report[20];
static uint8_t xid_rumble_buf[32];      // Receive buffer for EP OUT
static gamepad_state_t *g_state = NULL;
static uint8_t xid_ep_in = 0;
static uint8_t xid_ep_out = 0;
static bool xid_ep_in_busy = false;

// ---------------------------------------------------------------------------
// TinyUSB descriptor callbacks
// ---------------------------------------------------------------------------

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&xid_device_desc;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return xid_config_desc;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)index;
    (void)langid;
    static const uint16_t empty[] = {
        (TUSB_DESC_STRING << 8) | 4,
        0x0409  // English
    };
    return empty;
}

// ---------------------------------------------------------------------------
// Custom XID class driver callbacks
// ---------------------------------------------------------------------------

static void xid_driver_init(void) {
    memset(xid_input_report, 0, sizeof(xid_input_report));
    xid_input_report[0] = 0x00;  // Report ID
    xid_input_report[1] = 0x14;  // Length (20)
    xid_ep_in = 0;
    xid_ep_out = 0;
    xid_ep_in_busy = false;
    printf("[XID] Driver init\n");
}

static bool xid_driver_deinit(void) {
    return true;
}

static void xid_driver_reset(uint8_t rhport) {
    (void)rhport;
    xid_ep_in = 0;
    xid_ep_out = 0;
    xid_ep_in_busy = false;
    printf("[XID] Driver reset\n");
}

// Called when TinyUSB finds an interface descriptor during enumeration.
// We claim interfaces with class 0x58 (Xbox) and open their endpoints
// as INTERRUPT type — this is the key fix over vendor class.
static uint16_t xid_driver_open(uint8_t rhport, tusb_desc_interface_t const *desc_intf, uint16_t max_len) {
    // Only claim Xbox XID interfaces
    if (desc_intf->bInterfaceClass != 0x58 || desc_intf->bInterfaceSubClass != 0x42) {
        return 0;
    }

    uint16_t const drv_len = 9 + (desc_intf->bNumEndpoints * 7);  // intf + endpoints
    if (max_len < drv_len) return 0;

    printf("[XID] Opening XID interface (class 0x%02X/0x%02X, %d endpoints)\n",
           desc_intf->bInterfaceClass, desc_intf->bInterfaceSubClass,
           desc_intf->bNumEndpoints);

    // Walk through endpoint descriptors and open them
    uint8_t const *p_desc = (uint8_t const *)desc_intf;
    p_desc += 9;  // Skip interface descriptor

    for (int i = 0; i < desc_intf->bNumEndpoints; i++) {
        tusb_desc_endpoint_t const *ep_desc = (tusb_desc_endpoint_t const *)p_desc;

        if (ep_desc->bDescriptorType != TUSB_DESC_ENDPOINT) break;

        // Open the endpoint — TinyUSB reads bmAttributes from the descriptor,
        // so it will open as INTERRUPT (0x03) since that's what we specified
        if (!usbd_edpt_open(rhport, ep_desc)) {
            printf("[XID] Failed to open EP 0x%02X\n", ep_desc->bEndpointAddress);
            return 0;
        }

        if (tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_IN) {
            xid_ep_in = ep_desc->bEndpointAddress;
            printf("[XID] EP IN = 0x%02X (interrupt, %d byte, %dms)\n",
                   xid_ep_in, ep_desc->wMaxPacketSize, ep_desc->bInterval);
        } else {
            xid_ep_out = ep_desc->bEndpointAddress;
            printf("[XID] EP OUT = 0x%02X (interrupt, %d byte, %dms)\n",
                   xid_ep_out, ep_desc->wMaxPacketSize, ep_desc->bInterval);

            // Start listening for rumble data on EP OUT
            usbd_edpt_xfer(rhport, xid_ep_out, xid_rumble_buf, sizeof(xid_rumble_buf));
        }

        p_desc += 7;  // Next endpoint descriptor
    }

    // Immediately queue the first IN transfer so data is ready
    // when the Xbox sends its very first poll on EP IN
    if (xid_ep_in) {
        xid_ep_in_busy = true;
        usbd_edpt_xfer(rhport, xid_ep_in, xid_input_report, sizeof(xid_input_report));
        printf("[XID] First IN transfer queued\n");
    }

    printf("[XID] XID interface opened successfully\n");
    return drv_len;
}

// Class driver control_xfer_cb — handles CLASS-type requests (0xA1, 0x21)
// TinyUSB routes these based on interface recipient.
static bool xid_driver_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                        tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;

    uint8_t bmRequestType = request->bmRequestType;
    uint8_t bRequest = request->bRequest;

    // GET Report (Input): bmRequestType=0xA1, bRequest=0x01
    if (bmRequestType == 0xA1 && bRequest == 0x01) {
        printf("[XID] -> GET Report\n");
        uint16_t len = request->wLength;
        if (len > sizeof(xid_input_report)) len = sizeof(xid_input_report);
        return tud_control_xfer(rhport, request, (void *)xid_input_report, len);
    }

    // SET Report (Output/Rumble): bmRequestType=0x21
    // Xbox uses bRequest=0x05 (XID SET_REPORT) or bRequest=0x09 (HID SET_REPORT)
    // wValue=0x0200 = Output report. Accept and ignore (no wireless rumble).
    if (bmRequestType == 0x21 && (bRequest == 0x05 || bRequest == 0x09)) {
        static uint8_t rumble_tmp[8];
        uint16_t len = request->wLength;
        if (len > sizeof(rumble_tmp)) len = sizeof(rumble_tmp);
        return tud_control_xfer(rhport, request, rumble_tmp, len);
    }

    printf("[XID] -> CLASS request unhandled: bReq=0x%02X, STALL\n", bRequest);
    return false;
}

// ---------------------------------------------------------------------------
// VENDOR-type control requests (bmRequestType type=VENDOR, 0xC0/0xC1)
// TinyUSB routes ALL vendor requests here, NOT to the class driver.
// This is the global TinyUSB callback that overrides the weak stub.
// ---------------------------------------------------------------------------
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;

    uint8_t bRequest = request->bRequest;
    uint16_t wValue = request->wValue;

    // GET XID Descriptor: bRequest=0x06, wValue=0x4200
    if (bRequest == 0x06 && wValue == 0x4200) {
        printf("[XID] -> GET XID Descriptor\n");
        uint16_t len = request->wLength;
        if (len > sizeof(xid_descriptor)) len = sizeof(xid_descriptor);
        return tud_control_xfer(rhport, request, (void *)xid_descriptor, len);
    }

    // GET Capabilities (Input): bRequest=0x01, wValue=0x0100
    if (bRequest == 0x01 && wValue == 0x0100) {
        printf("[XID] -> GET Input Capabilities\n");
        uint16_t len = request->wLength;
        if (len > sizeof(xid_input_caps)) len = sizeof(xid_input_caps);
        return tud_control_xfer(rhport, request, (void *)xid_input_caps, len);
    }

    // GET Capabilities (Output): bRequest=0x01, wValue=0x0200
    if (bRequest == 0x01 && wValue == 0x0200) {
        printf("[XID] -> GET Output Capabilities\n");
        uint16_t len = request->wLength;
        if (len > sizeof(xid_output_caps)) len = sizeof(xid_output_caps);
        return tud_control_xfer(rhport, request, (void *)xid_output_caps, len);
    }

    // Unknown vendor request
    printf("[XID] -> VENDOR request unhandled, STALL\n");
    return false;
}

// Handle completed endpoint transfers
static bool xid_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                                 xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;
    (void)xferred_bytes;

    if (ep_addr == xid_ep_in) {
        // IN transfer complete — Xbox received our report
        xid_ep_in_busy = false;
    } else if (ep_addr == xid_ep_out) {
        // OUT transfer complete — received rumble data, ignore it
        // Re-arm the OUT endpoint to keep receiving
        usbd_edpt_xfer(rhport, xid_ep_out, xid_rumble_buf, sizeof(xid_rumble_buf));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Register our custom class driver with TinyUSB
// ---------------------------------------------------------------------------
static const usbd_class_driver_t xid_class_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name             = "XID",
#endif
    .init             = xid_driver_init,
    .deinit           = xid_driver_deinit,
    .reset            = xid_driver_reset,
    .open             = xid_driver_open,
    .control_xfer_cb  = xid_driver_control_xfer_cb,
    .xfer_cb          = xid_driver_xfer_cb,
    .sof              = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &xid_class_driver;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void xid_device_init(gamepad_state_t *state) {
    g_state = state;

    // Initialize input report to neutral state
    memset(xid_input_report, 0, sizeof(xid_input_report));
    xid_input_report[0] = 0x00;  // Report ID
    xid_input_report[1] = 0x14;  // Length (20)

    // Initialize TinyUSB
    tusb_init();

    printf("[XID] USB XID device initialized\n");
    printf("[XID] Presenting as OG Xbox Controller S (VID 0x045E, PID 0x0289)\n");
    printf("[XID] Using custom class driver with INTERRUPT endpoints\n");
}

void xid_device_task(void) {
    tud_task();
}

void xid_device_send_report(void) {
    if (!tud_ready()) return;
    if (!xid_ep_in) return;
    if (xid_ep_in_busy) return;

    // Submit the report on the interrupt IN endpoint
    // TinyUSB will send it when the Xbox next polls (every 4ms)
    xid_ep_in_busy = true;
    usbd_edpt_xfer(0, xid_ep_in, xid_input_report, sizeof(xid_input_report));
}

void xid_device_update(void) {
    if (!g_state) return;

    // [0] Report ID = 0x00
    xid_input_report[0] = 0x00;
    // [1] Length = 0x14 (20)
    xid_input_report[1] = 0x14;

    // [2] Digital buttons bitmask
    uint8_t digi = 0;
    if (g_state->dpad_up)     digi |= 0x01;
    if (g_state->dpad_down)   digi |= 0x02;
    if (g_state->dpad_left)   digi |= 0x04;
    if (g_state->dpad_right)  digi |= 0x08;
    if (g_state->start)       digi |= 0x10;
    if (g_state->back)        digi |= 0x20;
    if (g_state->left_thumb)  digi |= 0x40;
    if (g_state->right_thumb) digi |= 0x80;
    xid_input_report[2] = digi;

    // [3] Reserved
    xid_input_report[3] = 0x00;

    // [4-9] Analog face buttons (0x00 or 0xFF since DuchesS is digital)
    xid_input_report[4] = g_state->a ? 0xFF : 0x00;            // A
    xid_input_report[5] = g_state->b ? 0xFF : 0x00;            // B
    xid_input_report[6] = g_state->x ? 0xFF : 0x00;            // X
    xid_input_report[7] = g_state->y ? 0xFF : 0x00;            // Y
    xid_input_report[8] = g_state->right_bumper ? 0xFF : 0x00; // Black (= RB)
    xid_input_report[9] = g_state->left_bumper ? 0xFF : 0x00;  // White (= LB)

    // [10-11] Triggers (0-255, mapped from 0-1023)
    xid_input_report[10] = (uint8_t)(g_state->left_trigger * 255 / 1023);
    xid_input_report[11] = (uint8_t)(g_state->right_trigger * 255 / 1023);

    // [12-19] Sticks (signed 16-bit, little-endian)
    int16_t lx = g_state->left_stick_x;
    int16_t ly = g_state->left_stick_y;
    int16_t rx = g_state->right_stick_x;
    int16_t ry = g_state->right_stick_y;

    xid_input_report[12] = lx & 0xFF;
    xid_input_report[13] = (lx >> 8) & 0xFF;
    xid_input_report[14] = ly & 0xFF;
    xid_input_report[15] = (ly >> 8) & 0xFF;
    xid_input_report[16] = rx & 0xFF;
    xid_input_report[17] = (rx >> 8) & 0xFF;
    xid_input_report[18] = ry & 0xFF;
    xid_input_report[19] = (ry >> 8) & 0xFF;
}
