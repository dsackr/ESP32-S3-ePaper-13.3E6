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

// Blocking: plays a sine-wave tone at freq_hz for duration_ms. Simple
// speaker/amp smoke test — not part of any Fraimic-compatible behavior.
void beep(uint32_t freq_hz = 1000, uint32_t duration_ms = 300);

// True if the ES7210 responded to init on either I2C address. Doesn't
// confirm the captured audio is actually meaningful — see recordPcm().
bool isMicReady();

constexpr uint32_t kSampleRateHz = 16000;

}  // namespace audio
