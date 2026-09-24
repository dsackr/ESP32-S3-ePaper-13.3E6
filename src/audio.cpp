#include "audio.h"

#include <driver/gpio.h>

#include "pins.h"
#include "remote_log.h"

namespace audio {

bool init() {
    gpio_hold_dis((gpio_num_t)PIN_PA_ENABLE);
    pinMode(PIN_PA_ENABLE, OUTPUT);
    digitalWrite(PIN_PA_ENABLE, LOW);
    Log.println("AUDIO: disabled (power amp kept OFF for low power)");
    return true;
}

void setVolume(int percent) {
    (void)percent;
}

void setAmpEnabled(bool on) {
    (void)on;
    // Audio is completely disabled for low-power battery operation; keep PA off
    digitalWrite(PIN_PA_ENABLE, LOW);
}

size_t playPcm(const int16_t *samples, size_t sample_count) {
    (void)samples;
    (void)sample_count;
    return 0;
}

size_t recordPcm(int16_t *buffer, size_t max_samples) {
    (void)buffer;
    (void)max_samples;
    return 0;
}

bool isMicReady() {
    return false;
}

void beep(uint32_t freq_hz, uint32_t duration_ms) {
    (void)freq_hz;
    (void)duration_ms;
}

}  // namespace audio

