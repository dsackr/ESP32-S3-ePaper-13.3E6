#pragma once

// Shared between fraimic_api.cpp (JSON API) and web_portal.cpp (browser
// UI) so the two surfaces can't drift apart.
namespace device_info {

// Bump the trailing build number each time firmware code changes.
constexpr const char *kVendor = "DAS2";
constexpr const char *kFirmwareVersion = "DAS2.13-3.DIY.003";
constexpr const char *kDeviceType = "13.3\" E-Ink";

}  // namespace device_info
