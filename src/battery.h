#pragma once

#include <Arduino.h>

namespace battery {

struct Status {
    int percent;          // 0-100, estimated from voltage
    int voltage_mv;
    bool charging;
    bool cable_connected;
};

void init();
Status read();

}  // namespace battery
