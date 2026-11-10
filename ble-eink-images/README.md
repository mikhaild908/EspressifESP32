# BLE E-Ink Image Selector

An ESP32-WROOM-32E project that displays the existing Polgas or Lauren bitmap on a monochrome e-ink display when the matching UTF-8 command is written from nRF Connect.

## Hardware

The display driver, pins, and rotations follow the repository's `e-ink-polgas` and `e-ink-lauren` examples:

- Driver: `GxEPD2_213_B74`
- SPI: SCK 18, MISO 19, MOSI 23
- Display: CS 5, DC 17, RST 16, BUSY 4
- Polgas rotation: 3
- Lauren rotation: 2

These are example defaults, not confirmed for every Inland 2.13-inch panel. Verify the panel controller, wiring, and orientation against the actual display before powering it. The images are aspect-ratio-preserving and centered on the screen.

## Build and flash

Install PlatformIO, then run these commands from this directory:

```text
pio run
pio run --target upload
pio device monitor
```

The PlatformIO project uses the `esp32dev` board definition and `huge_app.csv` partition table because the two embedded bitmap assets are sizeable.

## Send an image command

1. Open nRF Connect and scan for `ESP32 E-Ink Images`.
2. Connect, open service `12345678-1234-1234-1234-123456789abc`, and select characteristic `abcdef01-1234-1234-1234-123456789abc`.
3. Use **Write** (not Write without response if your client setup lacks it) and send the exact lowercase UTF-8 text `polgas` or `lauren`.

Unknown commands, uppercase variants, and empty writes are ignored. The BLE callback only queues the selection; the main Arduino loop performs the slower e-ink refresh.

The bitmaps are copied from the repository's `e-ink-polgas` and `e-ink-lauren` projects into `include/`, so this project builds independently. Update those copied headers if the source images change.
