// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DuchesS Wireless contributors
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.md

/*
 * main.cpp - DuchesS Wireless Transmitter
 *
 * XIAO ESP32S3 firmware: reads DuchesS controller via USB Host (GIP)
 * and transmits as BLE HID gamepad to the Pi Pico 2 W receiver.
 *
 * Signal chain:
 *   DuchesS (USB/GIP) -> XIAO ESP32S3 (this) -> BLE -> Pico 2 W -> OG Xbox
 *
 * Hardware:
 *   - Single USB-C port: used for flashing (from PC) then USB Host (controller)
 *   - LED: GPIO21, active LOW
 *   - Debug UART: GPIO43(TX), GPIO44(RX), 115200 baud
 *
 * OPTIONAL LOW-BATTERY DETECTION (two methods, wire either or both;
 * both are safely inactive when left unconnected):
 *
 *   Method A - Analog battery sense on A0 (GPIO1):
 *     For charge/boost boards that expose the raw battery voltage,
 *     e.g. the Adafruit bq25185 5V Boost board's bottom-edge BAT pad.
 *     Wire: BAT pad -> 100k resistor -> A0 -> 100k resistor -> GND.
 *     The divider halves the battery voltage into the ADC's range.
 *     The firmware auto-detects the divider: it arms only after
 *     several consecutive readings in the plausible LiPo range, so an
 *     unwired (floating) pin can never trigger a false warning.
 *
 *   Method B - Digital low-battery input on D1 (GPIO2), active LOW:
 *     For charge/boost boards with a dedicated low-battery output
 *     (often labeled LBO, LB, or LOWBATT, e.g. Adafruit PowerBoost).
 *     The internal pull-up keeps this inactive when unconnected.
 *
 * LED status codes (LED is active LOW):
 *   Triple-blink burst = LOW BATTERY - charge before use!
 *   Fast blink (5/sec) = waiting for USB controller
 *   Slow blink (1/sec) = USB OK, waiting for BLE receiver
 *   Solid ON           = USB + BLE connected, fully operational
 *
 * WHY THE LOW-BATTERY INDICATOR EXISTS:
 *   A weak LiPo can light every status LED in the system (charger,
 *   ESP32, controller, even trigger the receiver's connect light)
 *   while still sagging under the ESP32-S3's radio transmit spikes
 *   and the USB host port's supply load - the result is "all lights
 *   on, but no controller inputs reach the Xbox." The dedicated
 *   triple-blink pattern makes that failure unmistakable.
 */

#include <Arduino.h>
#include "gamepad_state.h"
#include "usb_host_xbox.h"
#include "ble_output.h"

// Debug UART on GPIO43(TX) / GPIO44(RX) since USB port becomes Host
#define DEBUG_TX_PIN 43
#define DEBUG_RX_PIN 44
HardwareSerial DebugSerial(1);

static gamepad_state_t gamepad;

// XIAO ESP32S3 LED: GPIO21, active LOW (LOW = on, HIGH = off)
#define LED_PIN 21

// ---------------------------------------------------------------------------
// Low-battery detection configuration
// ---------------------------------------------------------------------------

// Method A: analog battery sense (XIAO A0). Battery voltage through a
// 100k/100k divider, so measured mV * 2 = battery mV.
#define BATT_SENSE_PIN        1      // GPIO1 = XIAO A0/D0
#define BATT_DIVIDER_RATIO    2      // 100k : 100k divider halves voltage
#define BATT_LOW_MV           3300   // warn below 3.30V
#define BATT_RECOVER_MV       3600   // clear warning above 3.60V
// Plausible single-cell LiPo range; readings outside this mean the
// divider is not wired (floating pin) and the method stays disarmed.
#define BATT_PLAUSIBLE_MIN_MV 2700
#define BATT_PLAUSIBLE_MAX_MV 4400
#define BATT_ARM_SAMPLES      5      // consecutive plausible reads to arm
#define BATT_SAMPLE_MS        1000   // sample interval

// Method B: digital low-battery input (XIAO D1). Active LOW, pull-up.
#define LOW_BATT_PIN     2
#define LOW_BATT_ON_MS   500         // continuously LOW this long -> warn
#define LOW_BATT_OFF_MS  2000        // continuously HIGH this long -> clear

// ---------------------------------------------------------------------------

static uint32_t last_status_print = 0;
static uint32_t last_ble_send     = 0;
static uint32_t last_led_toggle   = 0;
static bool     led_state         = false;

// Method A state
static bool     batt_sense_armed   = false;
static uint8_t  batt_arm_count     = 0;
static bool     batt_analog_low    = false;
static uint32_t batt_last_sample   = 0;
static uint32_t batt_mv            = 0;   // last measured battery voltage

// Method B state
static bool     lbo_active     = false;
static uint32_t lbo_last_high  = 0;
static uint32_t lbo_last_low   = 0;

void setup() {
    DebugSerial.begin(115200, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
    delay(500);

    DebugSerial.println("============================================");
    DebugSerial.println("  DuchesS Wireless Transmitter");
    DebugSerial.println("  XIAO ESP32S3 -> BLE -> Pico 2 W -> Xbox");
    DebugSerial.println("  Firmware v1.0");
    DebugSerial.println("============================================");
    DebugSerial.println();

    // LED off initially
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Method B input: pull-up keeps it inactive when unconnected
    pinMode(LOW_BATT_PIN, INPUT_PULLUP);
    uint32_t boot_ms = millis();
    lbo_last_high = boot_ms;
    lbo_last_low  = boot_ms;

    // Method A input: plain analog pin; auto-arms only if a divider
    // feeding plausible LiPo voltages is detected.
    analogReadResolution(12);

    gamepad_state_reset(&gamepad);

    // Start BLE first (begins advertising immediately)
    ble_output_init();

    // Then start USB Host for the DuchesS controller
    usb_host_xbox_init(&gamepad);

    DebugSerial.println("[MAIN] Setup complete!");
    DebugSerial.println("[MAIN] Connect DuchesS via USB-C OTG adapter");
    DebugSerial.println("[MAIN] Pico 2 W receiver auto-connects via BLE");
    DebugSerial.println();
}

// ---------------------------------------------------------------------------
// Low-battery detection task (both methods)
// ---------------------------------------------------------------------------
static void low_battery_task(uint32_t now) {
    // ---- Method A: analog battery sense ----
    if (now - batt_last_sample >= BATT_SAMPLE_MS) {
        batt_last_sample = now;

        // Calibrated millivolts at the pin; x2 for the 100k/100k divider
        uint32_t pin_mv = analogReadMilliVolts(BATT_SENSE_PIN);
        uint32_t measured_mv = pin_mv * BATT_DIVIDER_RATIO;

        bool plausible = (measured_mv >= BATT_PLAUSIBLE_MIN_MV &&
                          measured_mv <= BATT_PLAUSIBLE_MAX_MV);

        if (!batt_sense_armed) {
            // Require several consecutive plausible readings before
            // trusting the input - a floating pin wanders and will
            // not hold a steady LiPo-range value.
            if (plausible) {
                if (++batt_arm_count >= BATT_ARM_SAMPLES) {
                    batt_sense_armed = true;
                    batt_mv = measured_mv;
                    DebugSerial.printf("[BATT] Battery sense detected on A0 (%lumV)\n",
                                       (unsigned long)measured_mv);
                }
            } else {
                batt_arm_count = 0;
            }
        } else {
            batt_mv = measured_mv;
            if (!batt_analog_low && measured_mv < BATT_LOW_MV) {
                batt_analog_low = true;
                DebugSerial.printf("[BATT] LOW BATTERY (%lumV) - charge before use!\n",
                                   (unsigned long)measured_mv);
            } else if (batt_analog_low && measured_mv > BATT_RECOVER_MV) {
                batt_analog_low = false;
                DebugSerial.printf("[BATT] Battery recovered (%lumV)\n",
                                   (unsigned long)measured_mv);
            }
        }
    }

    // ---- Method B: digital LBO input (active LOW, hysteresis latch) ----
    if (digitalRead(LOW_BATT_PIN) == HIGH) {
        lbo_last_high = now;
    } else {
        lbo_last_low = now;
    }

    if (!lbo_active && (now - lbo_last_high >= LOW_BATT_ON_MS)) {
        lbo_active = true;
        DebugSerial.println("[BATT] LOW BATTERY (LBO pin) - charge before use!");
    } else if (lbo_active && (now - lbo_last_low >= LOW_BATT_OFF_MS)) {
        lbo_active = false;
        DebugSerial.println("[BATT] Battery level OK again (LBO pin)");
    }
}

static inline bool low_battery_active(void) {
    return batt_analog_low || lbo_active;
}

void loop() {
    uint32_t now = millis();

    // Process USB host events
    usb_host_xbox_task();

    // Send BLE reports at ~250Hz when new data arrives
    if (gamepad.updated && (now - last_ble_send >= 4)) {
        if (ble_output_connected()) {
            ble_output_send(&gamepad);
        }
        gamepad.updated = false;
        last_ble_send = now;
    }

    // Low-battery detection (both optional methods)
    low_battery_task(now);

    // LED indicator (active LOW: LOW = on, HIGH = off)
    //   Triple-blink burst = LOW BATTERY (overrides everything)
    //   Fast blink         = no USB controller
    //   Slow blink         = USB ok, no BLE connection
    //   Solid ON           = both USB and BLE connected
    if (low_battery_active()) {
        // Triple-blink burst: 3 short flashes, then a pause (1.3s cycle)
        uint32_t phase = now % 1300;
        bool on = (phase < 100) ||
                  (phase >= 200 && phase < 300) ||
                  (phase >= 400 && phase < 500);
        if (on != led_state) {
            led_state = on;
            digitalWrite(LED_PIN, on ? LOW : HIGH);
        }
        last_led_toggle = now;
    } else {
        bool usb_ok = usb_host_xbox_connected();
        bool ble_ok = ble_output_connected();

        if (usb_ok && ble_ok) {
            if (!led_state) {
                led_state = true;
                digitalWrite(LED_PIN, LOW);
            }
        } else if (usb_ok) {
            if (now - last_led_toggle >= 1000) {
                led_state = !led_state;
                digitalWrite(LED_PIN, led_state ? LOW : HIGH);
                last_led_toggle = now;
            }
        } else {
            if (now - last_led_toggle >= 200) {
                led_state = !led_state;
                digitalWrite(LED_PIN, led_state ? LOW : HIGH);
                last_led_toggle = now;
            }
        }
    }

    // Status print every 5 seconds
    if (now - last_status_print >= 5000) {
        last_status_print = now;
        DebugSerial.printf("[STATUS] USB:%s BLE:%s",
                   usb_host_xbox_connected() ? "OK" : "wait",
                   ble_output_connected() ? "CONNECTED" : "advertising");
        if (batt_sense_armed) {
            DebugSerial.printf(" BATT:%lumV(%s)",
                       (unsigned long)batt_mv,
                       low_battery_active() ? "LOW" : "ok");
        } else {
            DebugSerial.printf(" BATT:%s", low_battery_active() ? "LOW" : "ok");
        }
        if (gamepad.connected) {
            DebugSerial.printf(" LX=%d LY=%d LT=%d RT=%d",
                       gamepad.left_stick_x, gamepad.left_stick_y,
                       gamepad.left_trigger, gamepad.right_trigger);
        }
        DebugSerial.println();
    }

    delay(1);
}
