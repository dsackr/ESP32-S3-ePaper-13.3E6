#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "DEV_Config.h"
#include "EPD_13in3e.h"
#include "audio.h"
#include "battery.h"
#include "fraimic_api.h"
#include "sd_card.h"
#include "wifi_provisioning.h"

void setup() {
    Serial.begin(115200);

    DEV_Module_Init();
    EPD_13IN3E_Init();

    battery::init();

    if (!sd_card::init()) {
        Serial.println("SD card not mounted (continuing without it)");
    }

    if (!audio::init()) {
        Serial.println("Audio codec init failed (continuing without it)");
    }

    if (!wifi_provisioning::connectSaved()) {
        Serial.println("No WiFi / connect failed — starting setup portal");
        wifi_provisioning::runProvisioningPortal();  // does not return
    }

    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());

    // Real Fraimic frames are reachable at fraimic.local (see the HA/REST
    // API guide) — matched here rather than eframe's per-device unique
    // hostname extension, since this firmware targets stock behavior.
    if (MDNS.begin("fraimic")) {
        MDNS.addService("http", "tcp", 80);
    }

    fraimic_api::begin();
}

void loop() {
    // ESPAsyncWebServer handles requests on its own task; nothing to do here.
    delay(1000);
}
