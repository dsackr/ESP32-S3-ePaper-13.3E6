#pragma once

#include <ESPAsyncWebServer.h>

namespace fraimic_api {

// Registers the stock Fraimic REST API routes on a shared server: GET
// /api/info, GET /api/battery, POST /api/refresh, POST /api/image, POST
// /api/restart, POST /api/sleep. `server` is owned by the caller (main.cpp),
// which also owns web_portal's routes and calls server.begin() once both
// have registered. Call after WiFi is connected.
void begin(AsyncWebServer &server);

}  // namespace fraimic_api
