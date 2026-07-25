# Third-party code

This project vendors and adapts code from two upstream projects targeting
the same ESP32-S3-ePaper-13.3E6 board:

## Waveshare `ESP32-S3-ePaper-13.3E6` (Apache License 2.0)

<https://github.com/waveshareteam/ESP32-S3-ePaper-13.3E6>

- `lib/epd13in3e/DEV_Config.{h,cpp}`, `lib/epd13in3e/EPD_13in3e.{h,cpp}` —
  adapted from `example/Arduino-3.2.0/examples/03_E-Paper_Example`. Pin map,
  register/LUT constants, and SPI sequencing are unchanged; drawing/font
  helpers we don't need were dropped, and `EPD_13IN3E_DisplayFraimicBin()`
  is new code written for this project.
- `lib/es8311/es8311.{h,cpp}`, `lib/es8311/es8311_reg.h` — vendored
  unmodified from `example/Arduino-3.2.0/examples/02_Audio_out`. Upstream
  attributes this driver to Espressif Systems (Shanghai) CO LTD, 2015-2022,
  also Apache-2.0.
- Pin assignments in `include/pins.h` (I2S, I2C, SD_MMC, battery ADC
  channel) were read from this repo's `01_ADC_Test`, `02_Audio_out`, and
  `04_SD_Test` Arduino examples.

## `teatall/13.3inch_e-Paper_E-Frame` (MIT License)

<https://github.com/teatall/13.3inch_e-Paper_E-Frame>

A community-built digital photo frame firmware for this same board. Used to
cross-verify the pin map above and as the source for board build settings
in `platformio.ini` (32MB flash, `Huge APP`-equivalent partitioning,
OPI PSRAM, `ARDUINO_USB_CDC_ON_BOOT=0` to keep Serial on the CH343 UART
chip instead of racing native USB-CDC). No source files were copied from
this project.

## Not vendored

`lib/es7210/` (microphone ADC driver) is original code written for this
project from the public ES7210 register map used by Espressif's own
open-source codec drivers — see the header comment in `es7210.h`. Neither
upstream project above exercises the mic in Arduino, so unlike everything
above, it has not been cross-checked against a working example on this
exact board.
