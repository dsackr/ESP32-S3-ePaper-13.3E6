#pragma once

#include <Arduino.h>

namespace device_config {

// Loads (or generates on first boot) a stable per-device identifier, stored
// in NVS. Not part of the stock Fraimic API response, just useful locally
// (mDNS hostname suffix, logs).
String deviceKey();

}  // namespace device_config
