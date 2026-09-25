#include "fraimic_api.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <time.h>

#include "EPD_13in3e.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#include "DEV_Config.h"
#include "audio.h"
#include "battery.h"
#include "device_config.h"
#include "device_info.h"
#include "display_queue.h"
#include "low_battery.h"
#include "pins.h"
#include "remote_log.h"

// Endpoint set and JSON shapes mirror the stock Fraimic REST API plus the
// clone fields ha-digital-frames needs for discovery: device.device_key,
// wifi.mac, display.width_px/height_px, POST /sleepconfig.
//
// Delivery model: dumb LAN endpoint. Home Assistant (or any client) POSTs
// /api/image. There is no image pull and no cloud check-in. Battery frames
// wake on a timer, stay online for active_window_sec so HA/device_tracker
// can notice them and push, then deep-sleep for sleep_minutes.
namespace fraimic_api {

namespace {

using device_info::kDeviceType;
using device_info::kFirmwareVersion;
using device_info::kVendor;

// Physical panel is 1600×1200 landscape; Fraimic .bin is portrait 1200×1600
// (same as official 13.3" frames). Report the bin-native orientation so HA
// packs images correctly.
constexpr int kDisplayWidthPx = 1200;
constexpr int kDisplayHeightPx = 1600;

constexpr int kBootButtonPin = 0;  // standard ESP32-S3 BOOT button, active-low
constexpr const char *kNvsNs = "fraimic_pwr";

// Frame buffers in PSRAM — double-buffer so we can accept /api/image while
// the panel is still refreshing the other buffer.
uint8_t *frameBuf[2] = {nullptr, nullptr};
uint8_t *imageBuf = nullptr;   // last buffer queued/shown (for /api/refresh)
uint8_t *lastShown = nullptr;  // buffer display_queue may still be reading
uint8_t *receiving = nullptr;  // buffer current /api/image body writes into
bool imageRequestValid = false;
bool hasLastImage = false;
uint32_t lastRefreshMillis = 0;
bool hasRefreshed = false;

// Power policy (persisted in NVS).
bool alwaysOn = false;
uint32_t sleepMinutes = 15;
uint32_t activeWindowSec = 120;
String displayOrientation = "portrait";

// Scheduled daily wake (persisted in NVS). The device has no wall-clock of
// its own, so the schedule is stored purely in UTC: whoever sets it (HA,
// the web portal) converts their local hour/minute using the UTC offset
// they know at that moment, and only the UTC result is kept on-device.
// Every subsequent wake computes "seconds until next UTC hour:minute" against
// freshly-synced NTP time — no DST tables or timezone ever live on the ESP32.
bool scheduleEnabled = false;
uint8_t scheduleUtcHour = 6;
uint8_t scheduleUtcMinute = 0;
uint32_t scheduleDurationSec = 1800;
// Echoed back by GET /api/wake-schedule purely for operator convenience —
// not used in any on-device calculation.
int16_t lastSetLocalHour = -1;
int16_t lastSetLocalMinute = -1;
int32_t lastSetUtcOffsetMin = 0;

bool ntpStarted = false;

const char *kNtpServer1 = "pool.ntp.org";
const char *kNtpServer2 = "time.google.com";

// Stay-awake deadline (millis). Extended by noteActivity() / image push.
uint32_t awakeUntilMs = 0;
volatile bool showTaskRunning = false;
// Latest buffer waiting to paint while the panel is busy (single slot; newest wins).
uint8_t *pendingShowBuf = nullptr;
// Prevent re-entering deep sleep path while deferred sleep is armed.
bool sleepArmed = false;

bool contentTypeIsOctetStream(const String &ct) {
    String lower = ct;
    lower.toLowerCase();
    return lower.startsWith("application/octet-stream");
}

void loadPowerConfig() {
    Preferences prefs;
    prefs.begin(kNvsNs, false);
    alwaysOn = prefs.getBool("always_on", false);
    sleepMinutes = prefs.getUInt("sleep_min", 15);
    activeWindowSec = prefs.getUInt("active_sec", 120);
    displayOrientation = prefs.getString("orientation", "portrait");
    if (displayOrientation != "portrait" && displayOrientation != "landscape") {
        displayOrientation = "portrait";
    }
    scheduleEnabled = prefs.getBool("sched_on", false);
    scheduleUtcHour = (uint8_t)prefs.getUInt("sched_uh", 6);
    scheduleUtcMinute = (uint8_t)prefs.getUInt("sched_um", 0);
    scheduleDurationSec = prefs.getUInt("sched_dur", 1800);
    lastSetLocalHour = (int16_t)prefs.getInt("sched_lh", -1);
    lastSetLocalMinute = (int16_t)prefs.getInt("sched_lm", -1);
    lastSetUtcOffsetMin = (int32_t)prefs.getInt("sched_off", 0);
    // Migrate away from pull-based delivery: drop any stored pull URL.
    if (prefs.isKey("pull_url")) {
        prefs.remove("pull_url");
        Log.println("Power: cleared legacy pull_url (push-only firmware)");
    }
    prefs.end();
    if (sleepMinutes < 1) sleepMinutes = 1;
    if (sleepMinutes > 7 * 24 * 60) sleepMinutes = 7 * 24 * 60;
    if (activeWindowSec < 30) activeWindowSec = 30;
    if (activeWindowSec > 3600) activeWindowSec = 3600;
    if (scheduleUtcHour > 23) scheduleUtcHour = 23;
    if (scheduleUtcMinute > 59) scheduleUtcMinute = 59;
    if (scheduleDurationSec < 60) scheduleDurationSec = 60;
    if (scheduleDurationSec > 4 * 3600) scheduleDurationSec = 4 * 3600;
}

void savePowerConfig() {
    Preferences prefs;
    prefs.begin(kNvsNs, false);
    prefs.putBool("always_on", alwaysOn);
    prefs.putUInt("sleep_min", sleepMinutes);
    prefs.putUInt("active_sec", activeWindowSec);
    prefs.putString("orientation", displayOrientation);
    prefs.putBool("sched_on", scheduleEnabled);
    prefs.putUInt("sched_uh", scheduleUtcHour);
    prefs.putUInt("sched_um", scheduleUtcMinute);
    prefs.putUInt("sched_dur", scheduleDurationSec);
    prefs.putInt("sched_lh", lastSetLocalHour);
    prefs.putInt("sched_lm", lastSetLocalMinute);
    prefs.putInt("sched_off", lastSetUtcOffsetMin);
    if (prefs.isKey("pull_url")) prefs.remove("pull_url");
    prefs.end();
}

void extendAwakeWindow() {
    uint32_t windowSec = scheduleEnabled ? scheduleDurationSec : activeWindowSec;
    awakeUntilMs = millis() + windowSec * 1000UL;
}

// System clock is kept in UTC (no offset, no DST) — configTime(0, 0, ...)
// is called once WiFi is up; NTP fills it in asynchronously in the
// background. Safe to call repeatedly.
void beginNtpSync() {
    if (ntpStarted) return;
    ntpStarted = true;
    configTime(0, 0, kNtpServer1, kNtpServer2);
}

// Non-blocking-ish read of the current UTC time. `timeoutMs` is how long to
// wait for NTP if it hasn't completed yet (0 = just check, don't wait).
bool nowUtc(struct tm &out, uint32_t timeoutMs = 0) {
    return getLocalTime(&out, timeoutMs);
}

// Seconds from `nowTm`/`nowEpoch` until the next occurrence of
// scheduleUtcHour:scheduleUtcMinute:00 UTC (today if still ahead, else
// tomorrow).
uint64_t secondsUntilScheduledWake(const struct tm &nowTm, time_t nowEpoch) {
    struct tm targetTm = nowTm;
    targetTm.tm_hour = scheduleUtcHour;
    targetTm.tm_min = scheduleUtcMinute;
    targetTm.tm_sec = 0;
    time_t target = mktime(&targetTm);
    if (target <= nowEpoch) target += 24 * 60 * 60;
    return (uint64_t)(target - nowEpoch);
}

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

}  // namespace

void enterDeepSleepForSeconds(uint64_t seconds) {
    if (seconds < 10) seconds = 10;

    Log.println("SLEEP: shutting down peripherals...");

    // Put e-paper controllers to sleep and cut 3V3_OUT power rail
    EPD_13IN3E_PowerOff();

    // Ensure audio PA is disabled
    audio::setAmpEnabled(false);

    // Hold power-control pins LOW during deep sleep so rails stay off
    gpio_hold_en((gpio_num_t)EPD_PWR_PIN);
    gpio_hold_en((gpio_num_t)PIN_PA_ENABLE);
    gpio_deep_sleep_hold_en();

    // Graceful Wi-Fi shutdown
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Timer wake = next "check-in window" for HA push / device_tracker.
    uint64_t us = seconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(us);

    // Configure RTC IO for BOOT button (GPIO0) with pull-up sustained in sleep
    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0 /* wake on LOW */);

    Log.printf("SLEEP: deep sleep %llus (timer + BOOT button). Display power rail OFF.\n",
               (unsigned long long)seconds);
    Log.flush();
    delay(50);
    esp_deep_sleep_start();
}

namespace {

void enterDeepSleep(uint32_t minutes) {
    if (minutes < 1) minutes = 1;
    Log.printf("SLEEP: interval mode, %u min. Wake for %us online window.\n", (unsigned)minutes,
                (unsigned)activeWindowSec);
    enterDeepSleepForSeconds((uint64_t)minutes * 60ULL);
}

void doSleepNow() {
    // Manual /api/sleep: sleep for configured interval (same as battery cycle).
    enterDeepSleep(sleepMinutes);
}

// Wait until the panel is free, then paint the latest pendingShowBuf.
void showWhenFreeTask(void *arg) {
    (void)arg;
    showTaskRunning = true;
    while (pendingShowBuf != nullptr) {
        uint8_t *buf = pendingShowBuf;
        pendingShowBuf = nullptr;
        for (int i = 0; i < 120 && display_queue::busy(); i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (display_queue::requestDisplay(buf, EPD_13IN3E_FRAIMIC_BIN_BYTES)) {
            imageBuf = buf;
            lastShown = buf;
            hasLastImage = true;
            lastRefreshMillis = millis();
            hasRefreshed = true;
            Log.printf("IMAGE: display queued (deferred)\n");
            while (display_queue::busy()) {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            // Keep the radio up after a refresh so HA can send another image.
            extendAwakeWindow();
        } else {
            Log.printf("IMAGE: display still busy after wait — will retry if new pending\n");
            if (pendingShowBuf == nullptr) {
                pendingShowBuf = buf;
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
    showTaskRunning = false;
    vTaskDelete(nullptr);
}

void queueShow(uint8_t *buf) {
    if (buf == nullptr) return;
    if (!display_queue::busy() && !showTaskRunning &&
        display_queue::requestDisplay(buf, EPD_13IN3E_FRAIMIC_BIN_BYTES)) {
        imageBuf = buf;
        lastShown = buf;
        hasLastImage = true;
        lastRefreshMillis = millis();
        hasRefreshed = true;
        Log.printf("IMAGE: display queued\n");
        extendAwakeWindow();
        return;
    }
    pendingShowBuf = buf;
    if (showTaskRunning) {
        Log.printf("IMAGE: deferred show updated (latest wins)\n");
        return;
    }
    BaseType_t ok = xTaskCreate(showWhenFreeTask, "show_img", 4096, nullptr, 1, nullptr);
    if (ok != pdPASS) {
        showTaskRunning = false;
        Log.println("IMAGE: failed to create show task");
    } else {
        Log.printf("IMAGE: deferred show started\n");
    }
}

void pickReceiveBuffer() {
    if (frameBuf[0] == nullptr) {
        receiving = nullptr;
        return;
    }
    if (display_queue::busy() && lastShown == frameBuf[0] && frameBuf[1] != nullptr) {
        receiving = frameBuf[1];
    } else if (display_queue::busy() && lastShown == frameBuf[1] && frameBuf[0] != nullptr) {
        receiving = frameBuf[0];
    } else {
        receiving = frameBuf[0];
    }
}

// Shared by GET /api/info's "schedule" section and GET/POST /api/wake-schedule.
void fillScheduleJson(JsonObject &obj) {
    obj["enabled"] = scheduleEnabled;
    obj["utc_hour"] = scheduleUtcHour;
    obj["utc_minute"] = scheduleUtcMinute;
    obj["duration_sec"] = scheduleDurationSec;
    if (lastSetLocalHour >= 0) {
        obj["last_set_local_hour"] = lastSetLocalHour;
        obj["last_set_local_minute"] = lastSetLocalMinute;
        obj["last_set_utc_offset_min"] = lastSetUtcOffsetMin;
    } else {
        obj["last_set_local_hour"] = nullptr;
        obj["last_set_local_minute"] = nullptr;
        obj["last_set_utc_offset_min"] = nullptr;
    }

    struct tm t;
    if (nowUtc(t)) {
        char nowBuf[24];
        strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%SZ", &t);
        obj["time_synced"] = true;
        obj["current_utc_time"] = nowBuf;

        time_t nowEpoch = mktime(&t);
        uint64_t secs = secondsUntilScheduledWake(t, nowEpoch);
        time_t target = nowEpoch + (time_t)secs;
        struct tm targetTm;
        gmtime_r(&target, &targetTm);
        char nextBuf[24];
        strftime(nextBuf, sizeof(nextBuf), "%Y-%m-%dT%H:%M:%SZ", &targetTm);
        obj["next_wake_utc"] = nextBuf;
        obj["next_wake_in_sec"] = (uint32_t)secs;
    } else {
        obj["time_synced"] = false;
        obj["current_utc_time"] = nullptr;
        obj["next_wake_utc"] = nullptr;
        obj["next_wake_in_sec"] = nullptr;
    }
}

void handleInfo(AsyncWebServerRequest *request) {
    extendAwakeWindow();  // HA poll counts as presence activity

    JsonDocument doc;
    doc["firmware_version"] = kFirmwareVersion;
    doc["vendor"] = kVendor;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    bool connected = WiFi.status() == WL_CONNECTED;
    wifi["connected"] = connected;
    wifi["ssid"] = connected ? WiFi.SSID() : "";
    wifi["rssi"] = connected ? WiFi.RSSI() : 0;
    wifi["channel"] = connected ? WiFi.channel() : 0;
    wifi["ip"] = connected ? WiFi.localIP().toString() : "";
    wifi["mac"] = WiFi.macAddress();

    battery::Status bat = battery::read();
    JsonObject batteryObj = doc["battery"].to<JsonObject>();
    batteryObj["percent"] = bat.percent;
    batteryObj["voltage_mv"] = bat.voltage_mv;
    batteryObj["pin_mv"] = bat.pin_mv;
    batteryObj["adc_raw"] = bat.adc_raw;
    batteryObj["charging"] = bat.charging;
    batteryObj["cable_connected"] = bat.cable_connected;
    batteryObj["low_battery_shown"] = low_battery::wasDisplayed();

    JsonObject device = doc["device"].to<JsonObject>();
    device["registered"] = false;
    device["account_created"] = false;
    device["device_key"] = device_config::deviceKey();
    struct tm infoTm;
    if (nowUtc(infoTm)) {
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &infoTm);
        device["time_synced"] = true;
        device["local_time"] = buf;
    } else {
        device["time_synced"] = false;
        device["local_time"] = nullptr;
    }
    device["uptime_s"] = (uint32_t)(millis() / 1000);

    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["voice_recording"] = false;
    settings["keep_awake"] = alwaysOn;

    bool portrait = (displayOrientation != "landscape");
    JsonObject display = doc["display"].to<JsonObject>();
    display["device_type"] = kDeviceType;
    display["width_px"] = portrait ? kDisplayWidthPx : kDisplayHeightPx;
    display["height_px"] = portrait ? kDisplayHeightPx : kDisplayWidthPx;
    display["orientation"] = displayOrientation;
    if (hasRefreshed) {
        display["last_refresh"] = (double)lastRefreshMillis / 1000.0;
    } else {
        display["last_refresh"] = nullptr;
    }
    display["next_refresh"] = nullptr;

    JsonObject power = doc["power"].to<JsonObject>();
    power["sleep_minutes"] = sleepMinutes;
    power["active_window_sec"] = activeWindowSec;
    power["always_on"] = alwaysOn;
    // Push-only: never advertise a pull URL.
    power["pull_url_set"] = false;
    power["delivery"] = "push";
    if (!alwaysOn) {
        int32_t remain = (int32_t)(awakeUntilMs - millis());
        power["awake_remaining_sec"] = remain > 0 ? (remain / 1000) : 0;
    } else {
        power["awake_remaining_sec"] = nullptr;
    }

    JsonObject schedule = doc["schedule"].to<JsonObject>();
    fillScheduleJson(schedule);

    sendJson(request, 200, doc);
}

void handleBattery(AsyncWebServerRequest *request) {
    battery::Status bat = battery::read();
    JsonDocument doc;
    doc["percent"] = bat.percent;
    doc["voltage_mv"] = bat.voltage_mv;
    doc["pin_mv"] = bat.pin_mv;
    doc["adc_raw"] = bat.adc_raw;
    doc["charging"] = bat.charging;
    doc["cable_connected"] = bat.cable_connected;
    doc["low_battery_shown"] = low_battery::wasDisplayed();
    sendJson(request, 200, doc);
}

void handleTestLowBattery(AsyncWebServerRequest *request) {
    if (display_queue::busy()) {
        sendError(request, 503, "display_busy");
        return;
    }
    xTaskCreate([](void *) {
        low_battery::showScreen();
        vTaskDelete(nullptr);
    }, "low_bat_test", 4096, nullptr, 1, nullptr);
    JsonDocument doc;
    doc["status"] = "refreshing_low_battery";
    sendJson(request, 200, doc);
}

void handleRefresh(AsyncWebServerRequest *request) {
    if (!hasLastImage || imageBuf == nullptr) {
        sendError(request, 404, "no image to refresh");
        return;
    }
    extendAwakeWindow();
    queueShow(imageBuf);
    JsonDocument doc;
    doc["status"] = "refresh_started";
    sendJson(request, 200, doc);
}

void handleImageBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     size_t total) {
    if (index == 0) {
        pickReceiveBuffer();
        // Accept whenever the size/type match — never reject just because the
        // panel is mid-refresh (that was the HA "asleep" false positive).
        imageRequestValid = receiving != nullptr && total == EPD_13IN3E_FRAIMIC_BIN_BYTES &&
                             contentTypeIsOctetStream(request->contentType());
        if (!imageRequestValid) {
            Log.printf("IMAGE: reject start total=%u recv=%p busy=%d ct=%s\n", (unsigned)total,
                        receiving, display_queue::busy() ? 1 : 0, request->contentType().c_str());
        } else {
            Log.printf("IMAGE: accept %u bytes into %s (busy=%d)\n", (unsigned)total,
                        receiving == frameBuf[0] ? "buf0" : "buf1",
                        display_queue::busy() ? 1 : 0);
            extendAwakeWindow();
        }
    }
    if (imageRequestValid && receiving != nullptr &&
        index + len <= EPD_13IN3E_FRAIMIC_BIN_BYTES) {
        memcpy(receiving + index, data, len);
    }
}

void handleImageDone(AsyncWebServerRequest *request) {
    if (!contentTypeIsOctetStream(request->contentType())) {
        sendError(request, 501, "unsupported_content_type");
        return;
    }
    if (!imageRequestValid || receiving == nullptr) {
        sendError(request, 400, "invalid_image_size");
        return;
    }

    uint8_t *buf = receiving;
    extendAwakeWindow();
    queueShow(buf);

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
    deferredAction(doSleepNow);
}

// ha-digital-frames POST /sleepconfig — form body:
// minutes, active_sec, always_on (1/0/true/false).
//
// minutes      = deep-sleep interval between wake windows (battery tradeoff)
// active_sec   = how long to stay online after wake for HA to push
// always_on    = never deep-sleep (plugged / keep-awake)
void handleSleepConfig(AsyncWebServerRequest *request) {
    if (request->hasParam("minutes", true)) {
        long m = request->getParam("minutes", true)->value().toInt();
        if (m < 1) m = 1;
        if (m > 7 * 24 * 60) m = 7 * 24 * 60;
        sleepMinutes = (uint32_t)m;
    }
    if (request->hasParam("active_sec", true)) {
        long s = request->getParam("active_sec", true)->value().toInt();
        if (s < 30) s = 30;
        if (s > 3600) s = 3600;
        activeWindowSec = (uint32_t)s;
    }
    if (request->hasParam("always_on", true)) {
        String raw = request->getParam("always_on", true)->value();
        raw.toLowerCase();
        alwaysOn = (raw == "1" || raw == "true" || raw == "on" || raw == "yes");
    }
    if (request->hasParam("orientation", true)) {
        String o = request->getParam("orientation", true)->value();
        o.toLowerCase();
        if (o == "portrait" || o == "landscape") {
            displayOrientation = o;
        }
    }
    savePowerConfig();
    extendAwakeWindow();
    sleepArmed = false;

    char resp[200];
    snprintf(resp, sizeof(resp),
             "OK sleep_minutes=%u active_window_sec=%u always_on=%s delivery=push",
             (unsigned)sleepMinutes, (unsigned)activeWindowSec,
             alwaysOn ? "true" : "false");
    request->send(200, "text/plain", resp);
    Log.printf("Power config: always_on=%d sleep=%umin active=%us (push-only)\n",
                alwaysOn ? 1 : 0, (unsigned)sleepMinutes, (unsigned)activeWindowSec);
}

void handleOrientation(AsyncWebServerRequest *request) {
    if (request->hasParam("orientation", true)) {
        String o = request->getParam("orientation", true)->value();
        o.toLowerCase();
        if (o == "portrait" || o == "landscape") {
            displayOrientation = o;
            savePowerConfig();
            extendAwakeWindow();
            Log.printf("Orientation: set to %s\n", displayOrientation.c_str());
            JsonDocument doc;
            doc["status"] = "ok";
            doc["orientation"] = displayOrientation;
            sendJson(request, 200, doc);
            return;
        }
    }
    sendError(request, 400, "invalid orientation");
}

void handleGetWakeSchedule(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    fillScheduleJson(obj);
    sendJson(request, 200, doc);
}

// DAS2-native schedule API for Home Assistant (or the web portal) to arm a
// daily wake window. Body (form-encoded, like /sleepconfig):
//   enabled          1/0/true/false
//   hour, minute     wake time AS THE CALLER UNDERSTANDS IT (their local time)
//   utc_offset_min   minutes to ADD to UTC to get that local time (e.g. -240
//                    for EDT, -300 for EST). Required whenever hour/minute
//                    is sent, so the conversion to UTC is unambiguous.
//   duration_sec     how long to stay online after the scheduled wake
//
// hour/minute/utc_offset_min are converted to UTC once, here, and only the
// UTC hour:minute is kept on-device — see the scheduleUtcHour/Minute comment
// above. If the caller's local clock crosses a DST boundary, they just need
// to re-POST with the new utc_offset_min (HA already knows this).
void handleSetWakeSchedule(AsyncWebServerRequest *request) {
    if (request->hasParam("enabled", true)) {
        String raw = request->getParam("enabled", true)->value();
        raw.toLowerCase();
        scheduleEnabled = (raw == "1" || raw == "true" || raw == "on" || raw == "yes");
    }
    if (request->hasParam("duration_sec", true)) {
        long d = request->getParam("duration_sec", true)->value().toInt();
        if (d < 60) d = 60;
        if (d > 4 * 3600) d = 4 * 3600;
        scheduleDurationSec = (uint32_t)d;
    }
    if (request->hasParam("hour", true) && request->hasParam("minute", true)) {
        if (!request->hasParam("utc_offset_min", true)) {
            sendError(request, 400, "utc_offset_min required when setting hour/minute");
            return;
        }
        long hour = request->getParam("hour", true)->value().toInt();
        long minute = request->getParam("minute", true)->value().toInt();
        long offsetMin = request->getParam("utc_offset_min", true)->value().toInt();
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            sendError(request, 400, "hour must be 0-23, minute 0-59");
            return;
        }
        if (offsetMin < -720 || offsetMin > 840) {
            sendError(request, 400, "utc_offset_min out of range");
            return;
        }
        // UTC = local - utc_offset_min, normalized into a 0-1439 minute-of-day.
        long utcTotalMin = (hour * 60 + minute) - offsetMin;
        utcTotalMin = ((utcTotalMin % 1440) + 1440) % 1440;
        scheduleUtcHour = (uint8_t)(utcTotalMin / 60);
        scheduleUtcMinute = (uint8_t)(utcTotalMin % 60);
        lastSetLocalHour = (int16_t)hour;
        lastSetLocalMinute = (int16_t)minute;
        lastSetUtcOffsetMin = (int32_t)offsetMin;
    }

    savePowerConfig();
    extendAwakeWindow();
    sleepArmed = false;

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    fillScheduleJson(obj);
    sendJson(request, 200, doc);
    Log.printf("Schedule: enabled=%d utc=%02u:%02u duration=%us\n", scheduleEnabled ? 1 : 0,
                (unsigned)scheduleUtcHour, (unsigned)scheduleUtcMinute,
                (unsigned)scheduleDurationSec);
}

// ha-digital-frames still POSTs /pullurl on provision. This firmware is
// push-only: accept the call, clear any legacy URL, do not fetch.
void handlePullUrl(AsyncWebServerRequest *request) {
    Preferences prefs;
    prefs.begin(kNvsNs, false);
    if (prefs.isKey("pull_url")) prefs.remove("pull_url");
    prefs.end();

    request->send(200, "text/plain",
                  "OK push-only: pull URL ignored. Frame waits for HA POST /api/image "
                  "while awake.");
    Log.println("PULLURL: ignored (push-only firmware)");
    extendAwakeWindow();
}

}  // namespace

void begin(AsyncWebServer &server) {
    loadPowerConfig();
    frameBuf[0] = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);
    frameBuf[1] = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);
    imageBuf = frameBuf[0];
    receiving = frameBuf[0];
    if (!frameBuf[0] || !frameBuf[1]) {
        Log.printf("FATAL: PSRAM alloc buf0=%p buf1=%p\n", frameBuf[0], frameBuf[1]);
    }

    server.on("/api/info", HTTP_GET, handleInfo);
    server.on("/api/battery", HTTP_GET, handleBattery);
    server.on("/api/refresh", HTTP_POST, handleRefresh);
    server.on("/api/restart", HTTP_POST, handleRestart);
    server.on("/api/sleep", HTTP_POST, handleSleep);
    server.on("/api/image", HTTP_POST, handleImageDone, nullptr, handleImageBody);
    server.on("/sleepconfig", HTTP_POST, handleSleepConfig);
    server.on("/pullurl", HTTP_POST, handlePullUrl);
    server.on("/api/wake-schedule", HTTP_GET, handleGetWakeSchedule);
    server.on("/api/wake-schedule", HTTP_POST, handleSetWakeSchedule);
    server.on("/api/orientation", HTTP_POST, handleOrientation);
    server.on("/api/test-low-battery", HTTP_POST, handleTestLowBattery);

    beginNtpSync();
    extendAwakeWindow();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *causeStr = "reset";
    if (cause == ESP_SLEEP_WAKEUP_TIMER) causeStr = "timer";
    else if (cause == ESP_SLEEP_WAKEUP_EXT0) causeStr = "button";
    Log.printf("Power: always_on=%d sleep=%umin active=%us schedule=%d(%02u:%02uZ) orient=%s delivery=push wake=%s\n",
                alwaysOn ? 1 : 0, (unsigned)sleepMinutes, (unsigned)activeWindowSec,
                scheduleEnabled ? 1 : 0, (unsigned)scheduleUtcHour, (unsigned)scheduleUtcMinute,
                displayOrientation.c_str(),
                causeStr);
}

void loop() {
    // Check low battery condition (triggers warning screen and sleep if <= 5% on battery)
    if (low_battery::checkAndHandle()) return;

    if (alwaysOn || sleepArmed) return;
    if (display_queue::busy() || showTaskRunning) return;

    // Power policy: if plugged in (charging or powered by cable), stay awake!
    battery::Status bat = battery::read();
    if (bat.cable_connected || bat.charging) {
        extendAwakeWindow();
        return;
    }

    if ((int32_t)(millis() - awakeUntilMs) < 0) return;

    // Active window expired — deep sleep until next wake.
    sleepArmed = true;
    if (scheduleEnabled) {
        struct tm nowTm;
        if (nowUtc(nowTm, 3000)) {
            time_t nowEpoch = mktime(&nowTm);
            uint64_t secs = secondsUntilScheduledWake(nowTm, nowEpoch);
            Log.printf("WAKE: schedule mode; next wake in %llus (%02u:%02u UTC)\n",
                        (unsigned long long)secs, (unsigned)scheduleUtcHour,
                        (unsigned)scheduleUtcMinute);
            enterDeepSleepForSeconds(secs);
            return;
        }
        Log.println("WAKE: schedule enabled but NTP sync failed; falling back to interval sleep");
    }
    Log.printf("WAKE: active window ended; sleeping %u min\n", (unsigned)sleepMinutes);
    enterDeepSleep(sleepMinutes);
}

bool isAlwaysOn() { return alwaysOn; }
uint32_t getSleepMinutes() { return sleepMinutes; }
uint32_t getActiveWindowSec() { return activeWindowSec; }

String getOrientation() { return displayOrientation; }
bool setOrientation(const String &orientation) {
    if (orientation == "portrait" || orientation == "landscape") {
        displayOrientation = orientation;
        savePowerConfig();
        extendAwakeWindow();
        return true;
    }
    return false;
}

void noteActivity() { extendAwakeWindow(); }

}  // namespace fraimic_api
