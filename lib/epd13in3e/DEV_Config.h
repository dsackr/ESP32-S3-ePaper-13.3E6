// Hardware pin map and low-level bit-banged SPI for the EL133UF1 panel
// controller pair (master/slave ICs driving the left/right halves of the
// 1200x1600 Spectra 6 display).
//
// Adapted from Waveshare's ESP32-S3-ePaper-13.3E6 Arduino example
// (github.com/waveshareteam/ESP32-S3-ePaper-13.3E6, Apache-2.0) — see
// /NOTICE.md. Pin assignments verified against that example and the
// teatall/13.3inch_e-Paper_E-Frame community project (MIT) built on the
// same board.
#pragma once

#include <Arduino.h>
#include <stdint.h>

using UBYTE = uint8_t;
using UWORD = uint16_t;
using UDOUBLE = uint32_t;

// GPIO map (ESP32-S3-ePaper-13.3E6, silkscreen "9PIN" e-Paper header)
constexpr int EPD_SCK_PIN = 9;
constexpr int EPD_MOSI_PIN = 46;
constexpr int EPD_CS_M_PIN = 10;  // Master IC (left half, cols 0-599)
constexpr int EPD_CS_S_PIN = 3;   // Slave IC (right half, cols 600-1199)
constexpr int EPD_DC_PIN = 11;
constexpr int EPD_RST_PIN = 2;
constexpr int EPD_BUSY_PIN = 12;  // LOW while busy, HIGH when idle
constexpr int EPD_PWR_PIN = 1;

constexpr int GPIO_PIN_SET = 1;
constexpr int GPIO_PIN_RESET = 0;

inline void DEV_Digital_Write(int pin, int value) { digitalWrite(pin, value == 0 ? LOW : HIGH); }
inline int DEV_Digital_Read(int pin) { return digitalRead(pin); }
inline void DEV_Delay_ms(uint32_t ms) { delay(ms); }

UBYTE DEV_Module_Init(void);
void DEV_Module_Exit(void);
void GPIO_Mode(UWORD pin, UWORD mode);
void DEV_SPI_WriteByte(UBYTE data);
void DEV_SPI_Write_nByte(const UBYTE *data, UDOUBLE len);
