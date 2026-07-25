#include "device_config.h"
#include <Preferences.h>

namespace device_config {

String deviceKey() {
    Preferences prefs;
    prefs.begin("device", false);
    String key = prefs.getString("device_key", "");
    if (key.isEmpty()) {
        uint64_t mac = ESP.getEfuseMac();
        key = "ep133e6-" + String((uint32_t)(mac >> 32), HEX) + String((uint32_t)mac, HEX);
        prefs.putString("device_key", key);
    }
    prefs.end();
    return key;
}

}  // namespace device_config
