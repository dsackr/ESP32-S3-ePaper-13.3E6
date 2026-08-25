#pragma once

#include <Arduino.h>

namespace battery {

struct Status {
    int percent;          // 0-100, estimated from cell voltage
    int voltage_mv;       // estimated pack/cell voltage at the divider input
    int pin_mv;           // raw ADC pin voltage after calibration (debug)
    int adc_raw;          // last averaged raw counts (debug)
    bool charging;
    bool cable_connected;
};

void init();
Status read();

}  // namespace battery
