#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "DEV_Config.h"
#include "EPD_13in3e.h"
#include "audio.h"
#include "battery.h"
#include "display_queue.h"
#include "fraimic_api.h"
#include "remote_log.h"
#include "sd_card.h"
#include "web_portal.h"
#include "wifi_provisioning.h"

// Shared by fraimic_api (JSON API) and web_portal (browser UI) so both
// surfaces answer on the same port 80.
AsyncWebServer server(80);

void setup() {
    Log.begin(115200);

    DEV_Module_Init();
    EPD_13IN3E_Init();
    display_queue::begin();

    battery::init();

    if (!sd_card::init()) {
        Log.println("SD card not mounted (continuing without it)");
    }

    if (!audio::init()) {
        Log.println("Audio codec init failed (continuing without it)");
    }

    if (!wifi_provisioning::connectSaved()) {
        Log.println("No WiFi / connect failed — starting setup portal");
        wifi_provisioning::runProvisioningPortal();  // does not return
    }

    Log.print("Connected, IP: ");
    Log.println(WiFi.localIP());

    // A real Fraimic frame answers at fraimic.local, but that collides with
    // a genuine frame on the same network (mDNS has no reliable rename-on-
    // conflict here) — so this dev board advertises a per-device name
    // instead, using the same MAC-derived suffix as the setup AP
    // (EPaper-Setup-XXXX) for easy correlation.
    uint64_t mac = ESP.getEfuseMac();
    char mdnsHostname[32];
    snprintf(mdnsHostname, sizeof(mdnsHostname), "frame-%04X", (unsigned)(mac & 0xFFFF));
    if (MDNS.begin(mdnsHostname)) {
        MDNS.addService("http", "tcp", 80);
    }

    // Keep radio awake for large image uploads and HA pull fetches.
    WiFi.setSleep(false);

  // ====================
  // OTA update support
  // ====================
  ArduinoOTA.setHostname(mdnsHostname);
  ArduinoOTA.onStart([]() {
    Log.println("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    Log.println("[OTA] End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("[OTA] Progress: %u%%\r\n", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Log.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Log.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Log.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Log.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Log.println("Receive Failed");
    else if (error == OTA_END_ERROR) Log.println("End Failed");
  });
  ArduinoOTA.begin();

    fraimic_api::begin(server);
    web_portal::begin(server);
    server.begin();
}

void loop() {
    // Battery wake cycle: stay online for active_window_sec, then deep-sleep
    // for sleep_minutes (unless always_on). Image delivery is push-only
    // (HA POST /api/image). ESPAsyncWebServer needs no polling of its own.
    fraimic_api::loop();
    ArduinoOTA.handle();
    delay(200);
}
