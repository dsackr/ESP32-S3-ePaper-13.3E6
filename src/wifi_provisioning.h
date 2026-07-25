#pragma once

#include <Arduino.h>

namespace wifi_provisioning {

// Tries to join WiFi using credentials saved from a previous provisioning
// run. Returns true once connected (STA mode), false on timeout/no creds.
bool connectSaved(uint32_t timeout_ms = 15000);

// Blocks forever: hosts a SoftAP ("EPaper-Setup-XXXX") + DNS captive portal
// so a phone/laptop can scan and submit WiFi credentials. On a successful
// STA connection it saves the credentials to NVS and calls ESP.restart() —
// the caller never regains control on success.
void runProvisioningPortal();

}  // namespace wifi_provisioning
