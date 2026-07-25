#include "fraimic_api.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include "EPD_13in3e.h"
#include "battery.h"
#include "device_info.h"

// Endpoint set and JSON shapes mirror the stock (non-eframe-extended) Fraimic
// REST API: github.com/dsackr/Fraimic_eink_canvas_home_assistant_restAPI_guide
// and the equivalent stock-only routes in eframe's app.py. Deliberately
// omits eframe's extensions (device.device_key, display.width_px/height_px/
// orientation, GET/POST /api/settings) since this firmware targets the real
// device's documented behavior, not eframe's forward-looking additions.
namespace fraimic_api {

namespace {

using device_info::kDeviceType;
using device_info::kFirmwareVersion;

constexpr int kBootButtonPin = 0;  // standard ESP32-S3 BOOT button, active-low

uint8_t *imageBuf = nullptr;
bool imageRequestValid = false;
bool hasLastImage = false;
uint32_t lastRefreshMillis = 0;
bool hasRefreshed = false;

void sendJson(AsyncWebServerRequest *request, int code, JsonDocument &doc) {
    String body;
    serializeJson(doc, body);
    request->send(code, "application/json", body);
}

void sendError(AsyncWebServerRequest *request, int code, const char *error) {
    JsonDocument doc;
    doc["error"] = error;
    sendJson(request, code, doc);
}

// Runs `action` on a one-shot FreeRTOS task after a short delay, so the HTTP
// response for /api/restart or /api/sleep actually reaches the client before
// the device restarts/sleeps out from under the connection.
void deferredAction(void (*action)()) {
    struct Ctx {
        void (*fn)();
    };
    auto *ctx = new Ctx{action};
    xTaskCreate(
        [](void *arg) {
            auto *c = static_cast<Ctx *>(arg);
            vTaskDelay(pdMS_TO_TICKS(500));
            c->fn();
            delete c;
            vTaskDelete(nullptr);
        },
        "deferred_action", 4096, ctx, 1, nullptr);
}

void doRestart() { ESP.restart(); }

void doSleep() {
    pinMode(kBootButtonPin, INPUT_PULLUP);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)kBootButtonPin, 0 /* wake on LOW */);
    esp_deep_sleep_start();
}

void handleInfo(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["firmware_version"] = kFirmwareVersion;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    bool connected = WiFi.status() == WL_CONNECTED;
    wifi["connected"] = connected;
    wifi["ssid"] = connected ? WiFi.SSID() : "";
    wifi["rssi"] = connected ? WiFi.RSSI() : 0;
    wifi["channel"] = connected ? WiFi.channel() : 0;
    wifi["ip"] = connected ? WiFi.localIP().toString() : "";

    battery::Status bat = battery::read();
    JsonObject batteryObj = doc["battery"].to<JsonObject>();
    batteryObj["percent"] = bat.percent;
    batteryObj["voltage_mv"] = bat.voltage_mv;
    batteryObj["charging"] = bat.charging;
    batteryObj["cable_connected"] = bat.cable_connected;

    JsonObject device = doc["device"].to<JsonObject>();
    device["registered"] = false;
    device["account_created"] = false;
    // No SNTP client wired up yet — report honestly rather than fabricate a time.
    device["time_synced"] = false;
    device["local_time"] = nullptr;
    device["uptime_s"] = (uint32_t)(millis() / 1000);

    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["voice_recording"] = false;  // mic hardware works, capture pipeline isn't wired to the API yet
    settings["keep_awake"] = true;        // /api/sleep only fires on request, not on a schedule

    JsonObject display = doc["display"].to<JsonObject>();
    display["device_type"] = kDeviceType;
    if (hasRefreshed) {
        display["last_refresh"] = (double)lastRefreshMillis / 1000.0;
    } else {
        display["last_refresh"] = nullptr;
    }
    display["next_refresh"] = nullptr;

    sendJson(request, 200, doc);
}

void handleBattery(AsyncWebServerRequest *request) {
    battery::Status bat = battery::read();
    JsonDocument doc;
    doc["percent"] = bat.percent;
    doc["voltage_mv"] = bat.voltage_mv;
    doc["charging"] = bat.charging;
    doc["cable_connected"] = bat.cable_connected;
    sendJson(request, 200, doc);
}

void handleRefresh(AsyncWebServerRequest *request) {
    if (!hasLastImage) {
        sendError(request, 404, "no image to refresh");
        return;
    }
    EPD_13IN3E_DisplayFraimicBin(imageBuf, EPD_13IN3E_FRAIMIC_BIN_BYTES);
    lastRefreshMillis = millis();
    hasRefreshed = true;
    JsonDocument doc;
    doc["status"] = "refresh_started";
    sendJson(request, 200, doc);
}

void handleImageBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        imageRequestValid = imageBuf != nullptr && total == EPD_13IN3E_FRAIMIC_BIN_BYTES &&
                             request->contentType() == "application/octet-stream";
    }
    if (imageRequestValid && index + len <= EPD_13IN3E_FRAIMIC_BIN_BYTES) {
        memcpy(imageBuf + index, data, len);
    }
}

void handleImageDone(AsyncWebServerRequest *request) {
    if (request->contentType() != "application/octet-stream") {
        sendError(request, 501, "unsupported_content_type");
        return;
    }
    if (!imageRequestValid) {
        sendError(request, 400, "invalid_image_size");
        return;
    }

    EPD_13IN3E_DisplayFraimicBin(imageBuf, EPD_13IN3E_FRAIMIC_BIN_BYTES);
    hasLastImage = true;
    lastRefreshMillis = millis();
    hasRefreshed = true;

    JsonDocument doc;
    doc["status"] = "rendering";
    doc["bytes_received"] = EPD_13IN3E_FRAIMIC_BIN_BYTES;
    sendJson(request, 200, doc);
}

void handleRestart(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["status"] = "restarting";
    sendJson(request, 200, doc);
    deferredAction(doRestart);
}

void handleSleep(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["status"] = "sleeping";
    sendJson(request, 200, doc);
    deferredAction(doSleep);
}

}  // namespace

void begin(AsyncWebServer &server) {
    imageBuf = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);

    server.on("/api/info", HTTP_GET, handleInfo);
    server.on("/api/battery", HTTP_GET, handleBattery);
    server.on("/api/refresh", HTTP_POST, handleRefresh);
    server.on("/api/restart", HTTP_POST, handleRestart);
    server.on("/api/sleep", HTTP_POST, handleSleep);
    server.on(
        "/api/image", HTTP_POST, handleImageDone, nullptr,
        handleImageBody);
}

}  // namespace fraimic_api
