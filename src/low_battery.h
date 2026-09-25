#pragma once

#include <Arduino.h>

namespace low_battery {

// Decompress the embedded low battery image into dest (respecting orientation).
// dest must point to at least 960,000 bytes (EPD_13IN3E_FRAIMIC_BIN_BYTES).
bool decompressImage(uint8_t *dest, size_t destLen, bool portrait);

// Refreshes the e-paper panel synchronously with the low battery image.
// Safe to call from setup() or loop() or HTTP test endpoints.
bool showScreen();

// Checks battery status. If operating on battery and battery is <= 5%:
// - If not yet displayed during this discharge cycle, refreshes screen to the
//   low battery warning image, then immediately enters deep sleep.
// - If already displayed, skips refresh (saving remaining energy) and deep sleeps.
// If connected to external power or charging, resets the displayed latch.
// Returns true if sleep was initiated (does not return), or false if normal.
bool checkAndHandle();

// Status query / manual reset
bool wasDisplayed();
void resetDisplayed();

}  // namespace low_battery
