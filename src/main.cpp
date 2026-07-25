#include <Arduino.h>
#include <ESPAsyncWebServer.h>
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

    fraimic_api::begin(server);
    web_portal::begin(server);
    server.begin();
}

void loop() {
    // ESPAsyncWebServer handles requests on its own task; nothing to do here.
    delay(1000);
}
