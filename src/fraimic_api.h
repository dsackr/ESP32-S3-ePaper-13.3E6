#pragma once

namespace fraimic_api {

// Starts the async HTTP server (port 80) exposing the stock Fraimic REST
// API: GET /api/info, GET /api/battery, POST /api/refresh, POST /api/image,
// POST /api/restart, POST /api/sleep. Call once, after WiFi is connected.
void begin();

}  // namespace fraimic_api
