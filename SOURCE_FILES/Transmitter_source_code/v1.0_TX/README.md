# DuchesS Wireless — Transmitter (XIAO ESP32S3)

**Version:** v1.0

Firmware that converts a wired Hyperkin DuchesS USB controller into a
wireless controller for the Original Xbox.

**Signal chain:**

```
DuchesS (USB/GIP) -> XIAO ESP32S3 (this firmware) -> BLE -> Pi Pico 2 W receiver -> OG Xbox
```

This folder is the **transmitter** half. The Pi Pico 2 W **receiver**
firmware is in the [`receiver/`](../receiver) folder of this repository.

## Hardware

- Seeed Studio XIAO ESP32S3
- Hyperkin DuchesS controller, connected via USB-C OTG adapter
- 1-cell LiPo battery + charge/boost board supplying 5V to the XIAO's
  VUSB pin (the XIAO's own USB-C port is occupied by the controller in
  host mode). Reference board: **Adafruit bq25185 USB/DC/Solar Charger
  with 5V Boost** (Adafruit #6106) — battery on the JST port, board
  "+" (5V) output to XIAO VUSB, grounds common.
- Optional: USB-UART adapter on GPIO43(TX)/GPIO44(RX), 115200 baud,
  for debug output

> **Boost converter startup note (bq25185 board):** its TPS61023 boost
> converter can stall into loads that instantly draw more than ~200mA
> at power-up. The ESP32-S3 starts below that and only ramps up when
> the radio and USB host come alive moments later, so normal operation
> is fine — but if you add extra hardware to the 5V rail, keep its
> inrush in mind.

## Low-battery indicator (optional, recommended)

A weak LiPo can light every status LED in the system while still
sagging under the ESP32-S3's radio transmit spikes and the USB host
port's load — the result is "all lights on, but no controller inputs
reach the Xbox." The firmware supports two independent detection
methods. Wire either (or both); each is safely inactive when left
unconnected.

### Method A — battery voltage sense (works with the bq25185 board)

The bq25185 board exposes the raw battery voltage on its bottom-edge
**BAT** pad. Feed it to the XIAO through a divider:

```
BAT pad ──[100kΩ]──●──[100kΩ]── GND
                   │
              XIAO A0 (D0)
```

The divider halves the battery voltage into the ADC's range and draws
only ~21µA. The firmware auto-detects it: after a few seconds of
plausible readings it arms, then reports the live voltage on the debug
UART and warns below 3.30V (recovering above 3.60V). If nothing is
wired to A0, the method never arms and nothing changes.

### Method B — dedicated low-battery pin (LBO-style boards)

If your charge/boost board instead has a low-battery output (often
labeled **LBO**, **LB**, or **LOWBATT** — pulls LOW when the battery is
nearly empty), connect it to the XIAO's **D1 (GPIO2)**. Internal
pull-up; leave unconnected if unused.

## LED status codes

| Pattern              | Meaning                                        |
|----------------------|------------------------------------------------|
| Triple-blink burst   | **LOW BATTERY — charge before use!**           |
| Fast blink (5/sec)   | Waiting for the DuchesS controller (USB)       |
| Slow blink (1/sec)   | Controller OK, waiting for the receiver (BLE)  |
| Solid on             | Fully connected and operational                |

## Troubleshooting

**Everything lights up, but no inputs reach the Xbox.**
Almost always a **weak battery**. A partially charged LiPo has enough
power to light every LED — the charger board, the ESP32, the
controller's white light, even to trigger the receiver's connect
light — while still sagging under load. LEDs prove almost nothing
about available power.

1. Fully charge the battery, or power the XIAO's 5V input from a bench
   supply / USB source to rule power out.
2. Wire the low-battery indicator (above) and the triple-blink pattern
   will tell you directly.

**Controller inputs feel dead after reflashing.**
Power-cycle both the transmitter and the receiver. Both sides clear
their BLE bonds at boot and re-pair automatically.

**Serial port won't connect when flashing (Linux).**
Your user needs permission for the serial device. On Arch-based
distros: `sudo usermod -aG uucp $USER`, then log out and back in.

## Building

See [BUILDING.md](BUILDING.md) for the full build, merge, and flash
procedure, including the exact pinned library versions this firmware
requires.

## License

GNU GPL v3 or later — see the LICENSE file at the repository root —
**with an additional permission** under GPLv3 section 7 that allows
linking with Espressif's precompiled (binary-only) ESP-IDF libraries.
See [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md). Each source file
written for this project carries an
`SPDX-License-Identifier: GPL-3.0-or-later` header and a pointer to
that exception.

## Third-party code

- **ESP32-BLE-Gamepad** by lemmingDev — MIT License, vendored in
  [`lib/ESP32-BLE-Gamepad`](lib/ESP32-BLE-Gamepad) with its original
  [`license.txt`](lib/ESP32-BLE-Gamepad/license.txt).
  Source: <https://github.com/lemmingDev/ESP32-BLE-Gamepad>, upstream
  commit `33a3dee` (library version 0.7.4).
  **Modified:** `BleConnectionStatus.cpp` — one line added to
  `onConnect()` (`this->connected = true;`) so the connection is reported
  as soon as the link is up, rather than only after authentication.
- **NimBLE-Arduino** by h2zero — Apache License 2.0. Not included in this
  repository; PlatformIO downloads the pinned version (2.3.9) at build
  time.

## Disclaimer

This is an independent, non-commercial hobby project. It is not
affiliated with, endorsed by, or sponsored by Hyperkin or Microsoft.
"Hyperkin" and "DuchesS" are trademarks of their respective owner, and
"Xbox" is a trademark of Microsoft Corporation. These names are used
only to describe what hardware this project works with.
