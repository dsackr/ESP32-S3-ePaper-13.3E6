// Board pin map for ESP32-S3-ePaper-13.3E6.
//
// E-paper pins live in lib/epd13in3e/DEV_Config.h (owned by that driver).
// Everything else is verified against Waveshare's official
// 01_ADC_Test/02_Audio_out/04_SD_Test Arduino examples and the
// teatall/13.3inch_e-Paper_E-Frame community project — see /NOTICE.md.
#pragma once

#include <hal/adc_types.h>

// I2C bus shared by the ES8311 (speaker codec) and ES7210 (mic codec).
constexpr int PIN_I2C_SDA = 41;
constexpr int PIN_I2C_SCL = 42;

// I2S bus: BCLK/LRCK/MCLK are shared; DOUT feeds ES8311, DIN reads ES7210.
constexpr int PIN_I2S_MCLK = 14;
constexpr int PIN_I2S_BCLK = 21;
constexpr int PIN_I2S_LRCK = 47;
constexpr int PIN_I2S_DOUT = 45;
constexpr int PIN_I2S_DIN = 48;

// Speaker power-amplifier enable (NS4150B).
constexpr int PIN_PA_ENABLE = 13;

// SD_MMC 4-bit bus.
constexpr int PIN_SD_CLK = 6;
constexpr int PIN_SD_CMD = 7;
constexpr int PIN_SD_D0 = 5;
constexpr int PIN_SD_D1 = 4;
constexpr int PIN_SD_D2 = 16;
constexpr int PIN_SD_D3 = 15;

// Battery voltage sense (resistor divider into ADC1).
constexpr adc_channel_t PIN_BATTERY_ADC_CHANNEL = ADC_CHANNEL_7;
