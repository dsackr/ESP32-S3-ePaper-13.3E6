#pragma once

// Shared between fraimic_api.cpp (JSON API) and web_portal.cpp (browser
// UI) so the two surfaces can't drift apart.
namespace device_info {

// TODO: replace with whatever a real Fraimic 13.3" unit actually reports —
// these are placeholders, not observed values from genuine hardware.
constexpr const char *kFirmwareVersion = "1.0.0-esp32";
constexpr const char *kDeviceType = "13.3\" E-Ink";

}  // namespace device_info
