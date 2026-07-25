#pragma once

#include <Arduino.h>

namespace audio {

bool init();

// Volume 0-100, applied to the ES8311 DAC.
void setVolume(int percent);
void setAmpEnabled(bool on);

// Blocking write/read of 16-bit mono PCM samples over the shared I2S bus.
// Returns bytes actually transferred.
size_t playPcm(const int16_t *samples, size_t sample_count);
size_t recordPcm(int16_t *buffer, size_t max_samples);

constexpr uint32_t kSampleRateHz = 16000;

}  // namespace audio
