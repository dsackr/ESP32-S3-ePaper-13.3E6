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
// bin_data must stay valid and unmodified until the refresh completes —
// safe for the long-lived heap buffers callers already keep for this
// purpose (see fraimic_api.cpp / web_portal.cpp).
bool requestDisplay(const uint8_t *bin_data, size_t len);

}  // namespace display_queue
