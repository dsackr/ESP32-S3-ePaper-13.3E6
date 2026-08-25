#pragma once

#include <Arduino.h>

namespace device_config {

// Loads (or generates on first boot) a stable per-device identifier, stored
// in NVS. Exposed as device.device_key in GET /api/info so ha-digital-frames
// can discover the frame and keep its config-entry unique_id across IP
// changes. Also used by the web portal info page.
String deviceKey();

}  // namespace device_config
