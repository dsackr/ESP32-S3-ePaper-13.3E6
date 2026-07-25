// Minimal ES7210 4-channel ADC (microphone) driver.
//
// NOTE: unlike the panel/ES8311 drivers in this repo, this one is NOT lifted
// from a working Waveshare/community example for this exact board — none of
// the vendor's Arduino examples exercise the mic (only the ESP-IDF
// 02_Mic_test does, via Espressif's much larger esp_codec_dev/codec_board
// component, which this project intentionally avoids). This is a from-first-
// principles port of the well-known public ES7210 register sequence used by
// Espressif's own open-source codec drivers. Treat gain/TDM-slot register
// values as a starting point to verify on real hardware, not a known-good
// reference like the rest of lib/.
#pragma once

#include <Arduino.h>
#include <Wire.h>

// ADDR pin strapping determines the I2C address; try both if init fails.
constexpr uint8_t ES7210_ADDR_0 = 0x40;
constexpr uint8_t ES7210_ADDR_1 = 0x41;

class ES7210 {
public:
    // sda/scl must already be Wire.begin()'d (shared with the ES8311 codec).
    bool begin(uint8_t i2c_addr, uint32_t mclk_hz, uint32_t sample_rate_hz);

    // Input gain in dB, applied to all 2 active mic channels (0-2 -> mic1/2 only).
    void setMicGain(uint8_t gain_db);

    void reset();

private:
    uint8_t addr_ = ES7210_ADDR_0;

    bool writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
};
