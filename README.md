# ESP32-S3-ePaper-13.3E6 — Fraimic-compatible firmware

Arduino firmware (via PlatformIO) for Waveshare's [ESP32-S3-ePaper-13.3E6]
board, implementing the real Fraimic frame's stock REST API directly
on-device — the same 13.3" E Ink Spectra 6 (EL133UF1) panel, at the same
1600×1200 resolution, that a genuine Fraimic frame uses.

[ESP32-S3-ePaper-13.3E6]: https://docs.waveshare.com/ESP32-S3-ePaper-13.3E6

## Why Arduino, not ESP-IDF

`arduino-esp32` is built as a component on top of ESP-IDF, not a separate
stack — this project already drops into raw IDF APIs where Arduino's
wrappers fall short (ADC calibration, deep sleep + `ext0` wake). Going
Arduino let this project reuse Waveshare's own tested drivers for the two
hardest peripherals (the E6 panel's waveform/LUT sequence, the ES8311 audio
codec) instead of re-deriving them against raw IDF — see `NOTICE.md`.

## Status

| Subsystem | State |
| --- | --- |
| E-paper panel (init/clear/push/sleep) | Working — driver logic adapted 1:1 from Waveshare's example |
| Fraimic REST API (`/api/info`, `/api/battery`, `/api/refresh`, `/api/image`, `/api/restart`, `/api/sleep`) | Implemented, stock (non-eframe-extended) shape |
| Web portal (`/`, `/wifi`, `/upload`, `/info`, `/logs`, `/ota`) | Implemented, browser UI ported from a sibling project — see `src/web_portal.cpp`. Not implemented: orientation control, SD image gallery, fuel-gauge calibration (none apply to this board) |
| WiFi provisioning (SoftAP + captive portal) | Implemented |
| SD card | Implemented (mount only; nothing reads/writes it yet) |
| Speaker (ES8311) | Working — confirmed on real hardware via `audio::beep()` (`POST /beep`) |
| Microphone (ES7210) | Working — confirmed on real hardware via `POST /mic-test`, which reports amplitude stats (min/max/RMS) from a live capture rather than just init success |
| Battery percentage | ADC-based estimate; **verify `kDividerRatio` in `src/battery.cpp` against a multimeter** |
| Charging / cable-connected status | Not wired up — no confirmed ETA6098 status line found in the docs reviewed so far |
| `device_type` / `firmware_version` strings | Placeholders in `include/device_info.h` — swap in real Fraimic values if you can capture them from genuine hardware |

## Building

This project pins `platform` to the [pioarduino] fork of `platform-espressif32`
rather than PlatformIO's official one — the official registry version still
resolves to the old IDF4.4-based arduino-esp32 2.x core, which is missing
`ESP_I2S.h` and the ADC calibration API this firmware (and Waveshare's own
examples) use. pioarduino tracks current arduino-esp32 3.x releases.

[pioarduino]: https://github.com/pioarduino/platform-espressif32

**PlatformIO itself needs Python 3.10+.** If your system Python is older
(macOS ships 3.9), create a venv with a newer interpreter first, e.g.:

```bash
brew install python@3.13
python3.13 -m venv ~/.venvs/pio && source ~/.venvs/pio/bin/activate
pip install platformio
```

Then, from this repo:

```bash
pio run                 # build — verified clean (15.8% RAM, 28.3% flash used)
pio run -t upload       # flash over USB-C
pio device monitor      # serial log (115200 baud, via the CH343 UART chip)
```

First boot with no saved WiFi credentials starts a `EPaper-Setup-XXXX`
access point with a captive portal — connect and submit your network's
SSID/password, and the device saves them to NVS and reboots.

## Image format

`/api/image` expects the exact same `.bin` layout as a real Fraimic frame:
1200×1600, 4-bit indexed color (2px/byte), left-half rows (cols 0-599) then
right-half rows (cols 600-1199), 960,000 bytes total — see
`fraimic_bin_converter` for a ready-made image → `.bin` converter, and
`lib/epd13in3e/EPD_13in3e.h` for how it maps onto the panel's master/slave
chip selects.

## Hardware reference

See `NOTICE.md` for exactly which pin assignments and driver code came from
which upstream source, and `partitions.csv` / `platformio.ini` for board
build settings (32MB flash, OPI PSRAM, `Huge APP`-equivalent partitioning).
