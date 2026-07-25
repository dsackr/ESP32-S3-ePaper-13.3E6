// EL133UF1 dual-IC (master/slave) driver for the 13.3" Spectra 6 (E6) panel.
//
// Adapted from Waveshare's ESP32-S3-ePaper-13.3E6 Arduino example
// (github.com/waveshareteam/ESP32-S3-ePaper-13.3E6, Apache-2.0) — see
// /NOTICE.md.
#pragma once

#include "DEV_Config.h"
#include <stddef.h>

constexpr int EPD_13IN3E_WIDTH = 1200;
constexpr int EPD_13IN3E_HEIGHT = 1600;

// 4-bit device color codes. 0x4 is intentionally unused by the panel.
constexpr uint8_t EPD_13IN3E_BLACK = 0x0;
constexpr uint8_t EPD_13IN3E_WHITE = 0x1;
constexpr uint8_t EPD_13IN3E_YELLOW = 0x2;
constexpr uint8_t EPD_13IN3E_RED = 0x3;
constexpr uint8_t EPD_13IN3E_BLUE = 0x5;
constexpr uint8_t EPD_13IN3E_GREEN = 0x6;

// Bytes per master/slave half: (WIDTH/2 cols packed 2px/byte / 2 halves) * HEIGHT.
constexpr size_t EPD_13IN3E_HALF_BYTES = (EPD_13IN3E_WIDTH / 2 / 2) * EPD_13IN3E_HEIGHT;  // 480,000
constexpr size_t EPD_13IN3E_FRAIMIC_BIN_BYTES = EPD_13IN3E_HALF_BYTES * 2;                 // 960,000

void EPD_13IN3E_Init(void);
void EPD_13IN3E_Clear(UBYTE color);
void EPD_13IN3E_Sleep(void);

// Push a standard row-interleaved framebuffer (as produced by GUI_Paint-style
// drawing code): EPD_13IN3E_HEIGHT rows of EPD_13IN3E_WIDTH/2 bytes each.
void EPD_13IN3E_Display(const UBYTE *image);

// Push a Fraimic-format .bin buffer directly: EPD_13IN3E_FRAIMIC_BIN_BYTES
// bytes, laid out as ALL master/left-half rows (cols 0-599, packed 2px/byte)
// followed by ALL slave/right-half rows (cols 600-1199) — see
// fraimic_bin_converter's generate_binary_file(). This is exactly the byte
// order EPD_13IN3E_Display already streams to each chip-select, so no
// reshaping is needed; we just split the buffer in two and stream each half.
void EPD_13IN3E_DisplayFraimicBin(const uint8_t *bin_data, size_t len);

// Diagnostic-only: fills the panel with 6 horizontal color bars. Useful for
// verifying the panel/wiring during bring-up.
void EPD_13IN3E_Show6Block(void);
