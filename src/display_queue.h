#pragma once

#include <Arduino.h>

// EPD_13IN3E_DisplayFraimicBin() takes tens of seconds (waveform/LUT
// sequence for this panel). Calling it directly inside an HTTP handler
// blocks the ESPAsyncWebServer "async_tcp" task past its watchdog timeout
// and reboots the device — this module runs the actual refresh on a
// dedicated task instead, so handlers can respond immediately.
namespace display_queue {

// Starts the dedicated display task. Call once, before any requestDisplay().
void begin();

// True if a refresh is currently in progress — callers should reject new
// upload requests while busy rather than overwrite the buffer mid-refresh.
bool busy();

// Queues bin_data (length len) for display on the dedicated task. Returns
// false without queuing anything if a refresh is already in progress.
// Copies into an internal PSRAM buffer immediately, so the caller may reuse
// or overwrite its buffer as soon as this returns.
bool requestDisplay(const uint8_t *bin_data, size_t len);

// Diagnostic: paint master/left RED and slave/right BLUE.
bool requestHalfColorTest();

}  // namespace display_queue
