#include "audio.h"

#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>

#include "es7210.h"
#include "es8311.h"
#include "pins.h"

// I2S/I2C pin usage and ES8311 init sequence follow Waveshare's
// 02_Audio_out Arduino example exactly (see /NOTICE.md). ES7210 mic wiring
// (shared bus, DIN pin) is inferred from the same example's setPins() call,
// which configures a DIN pin the playback-only demo never reads from.
namespace audio {

namespace {

constexpr uint32_t kMclkMultiple = 256;
constexpr uint32_t kMclkFreqHz = kSampleRateHz * kMclkMultiple;

I2SClass i2s;
es8311_handle_t es8311Handle = nullptr;
ES7210 es7210;
bool micReady = false;

}  // namespace

bool init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    pinMode(PIN_PA_ENABLE, OUTPUT);
    setAmpEnabled(true);

    es8311Handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!es8311Handle) return false;

    const es8311_clock_config_t clkCfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = (int)kMclkFreqHz,
        .sample_frequency = (int)kSampleRateHz,
    };
    if (es8311_init(es8311Handle, &clkCfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        return false;
    }
    es8311_voice_volume_set(es8311Handle, 70, nullptr);
    es8311_microphone_config(es8311Handle, false);

    micReady = es7210.begin(ES7210_ADDR_0, kMclkFreqHz, kSampleRateHz) ||
               es7210.begin(ES7210_ADDR_1, kMclkFreqHz, kSampleRateHz);

    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, kSampleRateHz, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                   I2S_STD_SLOT_LEFT)) {
        return false;
    }

    return true;
}

void setVolume(int percent) {
    if (es8311Handle) es8311_voice_volume_set(es8311Handle, percent, nullptr);
}

void setAmpEnabled(bool on) { digitalWrite(PIN_PA_ENABLE, on ? HIGH : LOW); }

size_t playPcm(const int16_t *samples, size_t sample_count) {
    return i2s.write((const uint8_t *)samples, sample_count * sizeof(int16_t));
}

size_t recordPcm(int16_t *buffer, size_t max_samples) {
    if (!micReady) return 0;
    return i2s.readBytes((char *)buffer, max_samples * sizeof(int16_t)) / sizeof(int16_t);
}

bool isMicReady() { return micReady; }

void beep(uint32_t freq_hz, uint32_t duration_ms) {
    size_t sample_count = (size_t)((uint64_t)kSampleRateHz * duration_ms / 1000);
    int16_t *samples = (int16_t *)malloc(sample_count * sizeof(int16_t));
    if (!samples) return;

    constexpr int16_t kAmplitude = 8000;  // moderate volume, avoids clipping
    for (size_t i = 0; i < sample_count; i++) {
        float phase = 2.0f * PI * freq_hz * i / kSampleRateHz;
        samples[i] = (int16_t)(kAmplitude * sinf(phase));
    }

    playPcm(samples, sample_count);
    free(samples);
}

}  // namespace audio
