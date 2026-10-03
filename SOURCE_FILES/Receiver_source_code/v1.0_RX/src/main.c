// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * main.c
 *
 * DuchesS Wireless Receiver - Pi Pico 2 W
 *
 * Signal chain:
 *   DuchesS (USB/GIP) -> XIAO ESP32S3 (BLE gamepad) -> [wireless]
 *     -> Pico 2 W (this firmware: BLE client + USB XID device) -> OG Xbox
 *
 * Architecture:
 *   - TinyUSB initializes FIRST so the Xbox sees a controller at boot
 *   - BTStack initializes second and scans for the XIAO transmitter
 *   - Until BLE connects, the XID device sends neutral/idle reports
 *   - Once BLE connects, incoming gamepad data is translated to XID format
 *
 * Hardware:
 *   - Pico 2 W micro-USB port -> Xbox-to-USB adapter cable -> OG Xbox
 *   - Re-pairing is automatic: BLE bonds are cleared at every power-up
 *   - Onboard LED (via CYW43): indicates connection status
 *   - UART0 (GP0=TX, GP1=RX): debug serial output at 115200 baud
 *
 * LED patterns:
 *   Fast blink  = scanning for BLE gamepad
 *   Slow blink  = BLE connected, waiting for Xbox USB
 *   Solid ON    = both BLE and USB active
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"

#include "tusb.h"
#include "gamepad_state.h"
#include "ble_hid_client.h"
#include "xid_device.h"

// ---------------------------------------------------------------------------
// TinyUSB device callbacks — track USB enumeration state
// ---------------------------------------------------------------------------
void tud_mount_cb(void) {
    printf("[USB] *** MOUNTED — host sent SET_CONFIGURATION ***\n");
    fflush(stdout);
}

void tud_umount_cb(void) {
    printf("[USB] *** UNMOUNTED ***\n");
    fflush(stdout);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    printf("[USB] *** SUSPENDED ***\n");
    fflush(stdout);
}

void tud_resume_cb(void) {
    printf("[USB] *** RESUMED ***\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static gamepad_state_t gamepad;

// LED state
static uint32_t last_led_toggle = 0;
static bool led_on = false;

// Status printing
static uint32_t last_status_print = 0;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    // Initialize stdio (UART0 on GP0/GP1)
    stdio_init_all();
    sleep_ms(1000);  // Wait for UART to settle

    printf("\n");
    printf("=============================================\n");
    printf("  DuchesS Wireless Receiver\n");
    printf("  Pi Pico 2 W - BLE to OG Xbox Bridge\n");
    printf("  Firmware v1.0\n");
    printf("=============================================\n\n");

    // Initialize gamepad state to neutral
    gamepad_state_reset(&gamepad);

    // --- Step 1: Initialize USB XID device FIRST ---
    // This must happen before BLE so the Xbox can enumerate the
    // controller immediately at boot. The XID device sends idle
    // reports (sticks centered, no buttons) until BLE connects.
    printf("[MAIN] Step 1: Initializing USB XID device...\n");
    xid_device_init(&gamepad);

    // --- Step 2: Initialize CYW43 (WiFi/BLE chip) ---
    printf("[MAIN] Step 2: Initializing CYW43 wireless...\n");
    if (cyw43_arch_init()) {
        printf("[MAIN] ERROR: CYW43 init failed!\n");
        while (1) { tight_loop_contents(); }
    }
    printf("[MAIN] CYW43 initialized OK\n");

    // --- Step 3: Initialize BLE HID client ---
    printf("[MAIN] Step 3: Initializing BLE HID client...\n");
    ble_hid_client_init(&gamepad);

    printf("\n[MAIN] Setup complete!\n");
    printf("[MAIN] USB XID device active (Xbox should see a controller)\n");
    printf("[MAIN] BLE scanning for 'DuchesS Wireless' transmitter...\n");
    printf("\n");

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------
    printf("[MAIN] Entering main loop...\n");
    fflush(stdout);

    uint32_t last_xid_send = 0;

    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // --- TinyUSB task ---
        tud_task();

        // --- Update XID report data when BLE data arrives ---
        if (gamepad.updated) {
            xid_device_update();
            gamepad.updated = false;
        }

        // --- Send XID report every 4ms (match Xbox polling interval) ---
        // The Xbox polls the interrupt IN endpoint continuously.
        // We must keep it supplied with data or the Xbox thinks
        // the controller is unresponsive.
        if (now - last_xid_send >= 4) {
            last_xid_send = now;
            xid_device_send_report();
        }

        // --- LED patterns based on actual state ---
        {
            bool ble_ok = ble_hid_client_connected();
            bool usb_ok = tud_ready();

            if (ble_ok && usb_ok) {
                // Solid ON = both active
                if (!led_on) {
                    led_on = true;
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
                }
            } else if (ble_ok) {
                // Slow blink = BLE connected, no USB
                if (now - last_led_toggle >= 1000) {
                    led_on = !led_on;
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
                    last_led_toggle = now;
                }
            } else {
                // Fast blink = scanning
                if (now - last_led_toggle >= 200) {
                    led_on = !led_on;
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
                    last_led_toggle = now;
                }
            }
        }

        // --- Status report every 3 seconds ---
        if (now - last_status_print >= 3000) {
            last_status_print = now;
            printf("[STATUS] t=%lu BLE:%s USB:%s notifs=%lu\n",
                   (unsigned long)now,
                   ble_hid_client_connected() ? "READY" : "scanning",
                   tud_ready() ? "OK" : "wait",
                   (unsigned long)ble_hid_client_notification_count());
            fflush(stdout);
        }

        tight_loop_contents();
    }

    // Never reached
    cyw43_arch_deinit();
    return 0;
}
