# DuchesS Wireless — Receiver (Pi Pico 2 W)

**Version:** v1.0

BLE-to-OG-Xbox bridge firmware for the **Raspberry Pi Pico 2 W**.

Receives wireless BLE gamepad data from the DuchesS Wireless transmitter
(XIAO ESP32S3) and presents it to the **Original Xbox** as a wired
Controller S using the Xbox's XID USB protocol.

This folder is the **receiver** half. The transmitter firmware is in the
[`transmitter/`](../transmitter) folder of this repository. Use the v1.0
transmitter with the v1.0 receiver.

## Signal Chain

```
DuchesS controller (USB/GIP)
    ↓  wired USB
XIAO ESP32S3 (transmitter - BLE gamepad peripheral)
    ↓  wireless BLE
Pi Pico 2 W (THIS FIRMWARE - BLE central + USB XID device)
    ↓  USB via Xbox-to-USB adapter cable
Original Xbox controller port
```

## Hardware Required

| Component | Purpose |
|-----------|---------|
| Raspberry Pi Pico 2 W | Receiver board (BLE + USB) |
| Xbox-to-USB adapter cable | Converts the Xbox controller plug to USB |
| XIAO ESP32S3 + DuchesS | The transmitter side (separate firmware) |
| USB-UART adapter (optional) | For debug serial output on GP0/GP1 |

## Building

Tested with **Pico SDK 2.2.0** and **Arm GNU Toolchain 14.2** (GCC 14.2.1).
The target board (`pico2_w`) is already set in `CMakeLists.txt`.

### Option 1: Linux command line

1. Install the build tools:

   - Arch / EndeavourOS / Manjaro:
     ```bash
     sudo pacman -S --needed base-devel git cmake ninja python arm-none-eabi-gcc arm-none-eabi-newlib
     ```
   - Debian / Ubuntu / Raspberry Pi OS:
     ```bash
     sudo apt install build-essential git cmake ninja-build python3 gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
     ```

2. Get the Pico SDK 2.2.0 **with its submodules** (BTstack, the CYW43
   wireless driver and TinyUSB live in submodules — the build fails
   without them):
   ```bash
   git clone -b 2.2.0 https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
   cd ~/pico-sdk
   git submodule update --init
   ```

3. Build, from this `receiver` folder:
   ```bash
   export PICO_SDK_PATH=~/pico-sdk
   cmake -B build -G Ninja
   cmake --build build
   ```
   Instead of the `export`, you can pass the path directly:
   `cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk`

   The first configure downloads and builds `picotool` (a Raspberry Pi
   helper tool), so it needs an internet connection and takes a little
   longer.

4. The firmware is `build/duchess_receiver.uf2`.

### Option 2: VS Code with the Raspberry Pi Pico extension (Windows, macOS, Linux)

1. Install the **Raspberry Pi Pico** extension. It downloads the SDK,
   toolchain, CMake and Ninja for you.
2. Use **Import Project** on this `receiver` folder and choose **SDK 2.2.0**.
3. Click **Compile**. The output is `build/duchess_receiver.uf2`.

## Flashing

The Pico 2 W uses drag-and-drop flashing — no programmer needed.

1. **Hold the BOOTSEL button** on the Pico 2 W
2. While holding it, **plug the Pico into your computer via micro-USB**
3. Release BOOTSEL — the Pico appears as a USB drive named **RP2350**
4. **Copy `duchess_receiver.uf2` onto that drive**
5. The Pico reboots automatically and starts running the firmware

Pre-built `.uf2` files are attached to the project's GitHub Releases.

## Usage

### First Time Setup

1. Flash the receiver firmware onto the Pico 2 W (see above)
2. Flash the transmitter firmware onto the XIAO ESP32S3 (see `transmitter/`)
3. Power on the XIAO with the DuchesS controller connected
4. Connect the Pico 2 W to the OG Xbox via the Xbox-to-USB adapter cable
5. The Pico scans for and connects to the transmitter automatically
6. Once connected, the Xbox sees a controller

### Normal Operation

1. Plug the Pico 2 W into the Xbox (via the adapter cable) — it immediately
   appears as a Controller S to the Xbox
2. Power on the XIAO transmitter with the DuchesS controller
3. The Pico auto-connects via BLE within a few seconds
4. Play!

### Re-pairing / Troubleshooting BLE

Pairing is automatic. Both the receiver and the transmitter clear their
stored BLE pairing data every time they power up, so if the connection ever
gets stuck (for example after reflashing either side), **power-cycle both
the transmitter and the receiver** and they will pair again from scratch.

If everything lights up but no inputs reach the Xbox, check the
transmitter's battery first — see the Troubleshooting section in the
transmitter README.

## LED Indicators

| Pattern | Meaning |
|---------|---------|
| Fast blink (5 Hz) | Scanning for the transmitter |
| Slow blink (1 Hz) | BLE connected, Xbox USB not yet active |
| Solid ON | Both BLE and Xbox USB active — ready to play |

## Button Mapping (DuchesS → OG Xbox)

| DuchesS | OG Xbox |
|---------|---------|
| A | A (analog 0xFF) |
| B | B (analog 0xFF) |
| X | X (analog 0xFF) |
| Y | Y (analog 0xFF) |
| White / LB (Left Bumper) | White (analog 0xFF) |
| Black / RB (Right Bumper) | Black (analog 0xFF) |
| Start / Menu | Start |
| Back / View | Back |
| Left Stick Click | Left Stick Click |
| Right Stick Click | Right Stick Click |
| D-Pad | D-Pad |
| Left Stick | Left Stick |
| Right Stick | Right Stick |
| Left Trigger | Left Trigger (scaled 0-255) |
| Right Trigger | Right Trigger (scaled 0-255) |

Note: The OG Xbox Controller S has analog (pressure-sensitive) face
buttons. The DuchesS face buttons are digital, so they send either 0x00
(off) or 0xFF (fully pressed).

## Technical Details

### XID Protocol

The OG Xbox uses a USB protocol called XID (Xbox Input Devices):
- Interface class `0x58` ('X'), subclass `0x42` ('B')
- Not standard USB HID — no HID descriptors
- Vendor-specific control transfers for device identification
- 20-byte input reports, 6-byte output reports (rumble)
- Identifies as VID `0x045E` / PID `0x0289` (Controller S), which the
  Xbox requires in order to accept the controller
- USB 1.1 Full Speed, 4 ms polling interval

### BLE Connection

The receiver acts as a BLE Central (client):
- Scans for a device named exactly "DuchesS Wireless"
- Connects with Just Works pairing + bonding
- Discovers the HID service (UUID 0x1812)
- Subscribes to input report notifications (15-byte reports: buttons,
  six axes, then the hat switch as the last byte)
- Auto-reconnects on disconnection

The transmitter requests a 7.5–8.75 ms BLE connection interval for low
latency.

### Latency Budget (estimated)

| Stage | Latency |
|-------|---------|
| DuchesS → XIAO GIP read | ~1-2 ms |
| XIAO BLE notification queue | ~0.5 ms |
| BLE connection interval | 0-8.75 ms |
| BLE air time + processing | ~0.5-1 ms |
| Pico mapping + XID buffer | ~0.1 ms |
| Xbox USB poll | 0-4 ms (avg 2 ms) |
| **Total end-to-end** | **~5-15 ms typical** |

## Project Structure

```
receiver/
├── CMakeLists.txt              # Build configuration
├── pico_sdk_import.cmake       # Locates the Pico SDK (from Raspberry Pi)
├── README.md                   # This file
├── LICENSE-EXCEPTION.md        # GPLv3 section 7 additional permission
└── src/
    ├── btstack_config.h        # BTstack configuration
    ├── tusb_config.h           # TinyUSB configuration
    ├── gamepad_state.h/.c      # Shared gamepad state
    ├── ble_hid_client.h/.c     # BLE Central HID client
    ├── xid_device.h/.c         # OG Xbox XID USB device
    └── main.c                  # Entry point, ties everything together
```

## Debug Output

Connect a USB-UART adapter to the Pico's UART0 pins:
- **GP0** (Pin 1) = TX → connect to adapter's RX
- **GP1** (Pin 2) = RX → connect to adapter's TX (optional)
- **GND** → connect to adapter's GND

Open a serial terminal at **115200 baud** to see debug output.

## License

GNU GPL v3 or later — see the LICENSE file at the repository root —
**with an additional permission** under GPLv3 section 7 that allows
combining this firmware with BTstack and the CYW43 wireless driver from the
Raspberry Pi Pico SDK. See [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md).
Each source file carries an `SPDX-License-Identifier: GPL-3.0-or-later`
header and a pointer to that exception.

## Third-party components

None of these are included in this folder; they come with the Pico SDK
when you build.

- **Raspberry Pi Pico SDK** — BSD-3-Clause.
  `pico_sdk_import.cmake` in this folder is Raspberry Pi's standard helper
  file, included unmodified with its BSD-3-Clause notice.
- **TinyUSB** — MIT License.
- **BTstack** by BlueKitchen GmbH — BlueKitchen license (non-commercial),
  plus Raspberry Pi's license for use on Raspberry Pi hardware.
- **cyw43-driver** by George Robotics (with the Infineon CYW43439 firmware)
  — George Robotics license (non-commercial) in the version used by SDK
  2.2.0, plus Raspberry Pi's license for use on Raspberry Pi hardware.

## Credits

- [xboxdevwiki.net](https://xboxdevwiki.net/Xbox_Input_Devices) — XID
  protocol documentation
- [OGX-Mini](https://github.com/wiredopposite/OGX-Mini) — consulted as a
  reference for XID on RP2040/RP2350 (no code from it is used here)
- [BTstack](https://github.com/bluekitchen/btstack),
  [TinyUSB](https://github.com/hathach/tinyusb),
  [Pico SDK](https://github.com/raspberrypi/pico-sdk)

## Disclaimer

This is an independent, non-commercial hobby project. It is not affiliated
with, endorsed by, or sponsored by Microsoft, Hyperkin, or Raspberry Pi.
"Xbox" is a trademark of Microsoft Corporation; "Hyperkin" and "DuchesS"
are trademarks of their respective owner. These names, and the Controller S
USB identifiers the Xbox requires, are used only for compatibility and to
describe what hardware this project works with.
