# Building the DuchesS Wireless Transmitter (XIAO ESP32S3)

## Requirements
- PlatformIO Core (CLI) 6.x — `pipx install platformio`
- Internet access on first build (downloads espressif32 platform + toolchain)

## Build

From this folder (the one containing `platformio.ini`):

```bash
pio run
```

Artifacts land in `.pio/build/seeed_xiao_esp32s3/`:
`bootloader.bin`, `partitions.bin`, `firmware.bin`.

> If you previously built with different library versions, do a clean
> build first: `rm -rf .pio && pio run`

## Create the single-file merged image (for web flashers)

```bash
cd .pio/build/seeed_xiao_esp32s3

pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin \
  -o merged.bin \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 firmware.bin
```

> **Note:** the `boot_app0.bin` path above assumes PlatformIO's default
> location on Linux/macOS. On Windows use
> `%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin`,
> or wherever your PlatformIO core directory is (`pio system info` shows it).

`merged.bin` flashes as a single file at offset `0x0`.

## Flash (PlatformIO direct)

Hold BOOT on the XIAO, plug it into your computer, release BOOT, then:

```bash
pio run -t upload
```

## Flash (web flasher method)

1. Hold the BOOT button on the XIAO ESP32S3
2. Plug in the USB-C cable, then release BOOT
3. Open a Web-Serial flasher (e.g. esptool.spacehuhn.com) in a
   Chromium-based browser and Connect
4. Single entry: offset `0x0`, file `merged.bin`
5. Erase (recommended), then Program
6. Unplug/replug the XIAO to boot the new firmware

Linux note: your user must be able to open the serial port
(`uucp` group on Arch: `sudo usermod -aG uucp $USER`, then re-login).

## Before publishing binaries

Verify no local paths leaked into the artifacts:

```bash
strings merged.bin | grep -iE '/home/|users' && echo "LEAK FOUND" || echo "clean"
```

## Version pins — do not bump casually

| Component            | Version | Why pinned                                            |
|----------------------|---------|-------------------------------------------------------|
| espressif32 platform | 6.9.0   | Matches the verified-working release build             |
| NimBLE-Arduino       | 2.3.9   | Exact version of the verified-working build (verified byte-identical to the official GitHub 2.3.9 tag, so the registry pin is safe). |
| ESP32-BLE-Gamepad    | vendored in `lib/` | The registry's "0.7.4" changed content over time WITHOUT a version bump - connection-status semantics and the HID report descriptor were altered, silently breaking pairing/input flow. The exact verified-working source (including a local fix in BleConnectionStatus.cpp) is committed in `lib/ESP32-BLE-Gamepad` and used automatically. Never replace it with a registry copy. |

Do not add ESP32-BLE-Gamepad back to `lib_deps` - the local `lib/` copy
must be the one that builds.

## After flashing: if it doesn't work

See the Troubleshooting section in [README.md](README.md) first —
especially the weak-battery failure mode, which mimics several other
problems. Always do first functional tests on solid power (bench supply
or full battery), never on a partially charged LiPo.
