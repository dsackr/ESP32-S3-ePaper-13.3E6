#pragma once

#include <ESPAsyncWebServer.h>

namespace fraimic_api {

// Registers the stock Fraimic REST API routes on a shared server: GET
// /api/info, GET /api/battery, POST /api/refresh, POST /api/image, POST
// /api/restart, POST /api/sleep, plus ha-digital-frames clone endpoints
// POST /sleepconfig and POST /pullurl (pullurl is accepted as a no-op —
// this firmware is push-only). `server` is owned by the caller (main.cpp).
void begin(AsyncWebServer &server);

// Call from loop(): battery wake cycle (stay online for active_window_sec,
// then deep-sleep for sleep_minutes unless always_on).
void loop();

// Power settings (persisted; also set via POST /sleepconfig).
bool isAlwaysOn();
uint32_t getSleepMinutes();
uint32_t getActiveWindowSec();

// Reset the "stay awake" deadline so HA has time to push after activity.
void noteActivity();

}  // namespace fraimic_api
