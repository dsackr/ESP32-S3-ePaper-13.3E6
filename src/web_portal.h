#pragma once

#include <ESPAsyncWebServer.h>

namespace web_portal {

// Registers the browser-facing "Fraimic Portal" UI on a shared server:
// GET / and /portal (landing page), /wifi (settings), /upload (manual
// image push), /info (device info), /logs (live log viewer), /ota
// (browser firmware update). `server` is owned by the caller (main.cpp),
// which also owns fraimic_api's routes and calls server.begin() once both
// have registered. Call after WiFi is connected.
void begin(AsyncWebServer &server);

}  // namespace web_portal
