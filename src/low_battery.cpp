#include "low_battery.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "EPD_13in3e.h"
#include "battery.h"
#include "display_queue.h"
#include "fraimic_api.h"
#include "remote_log.h"

namespace low_battery {

// Flag in RTC slow memory surviving deep sleep cycles.
// Tracks whether the low battery image has been displayed during this discharge.
RTC_DATA_ATTR static bool lowBatteryDisplayed = false;

bool wasDisplayed() {
    return lowBatteryDisplayed;
}

void resetDisplayed() {
    lowBatteryDisplayed = false;
}

bool showScreen() {
    // If a display refresh is in progress, wait for it to complete
    while (display_queue::busy()) {
        delay(100);
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Log.println("BAT: PSRAM alloc failed for low battery image");
        return false;
    }

    bool portrait = (fraimic_api::getOrientation() != "landscape");
    if (!decompressImage(buf, EPD_13IN3E_FRAIMIC_BIN_BYTES, portrait)) {
        Log.println("BAT: Decompression failed for low battery image");
        free(buf);
        return false;
    }

    Log.printf("BAT: Refreshing panel with low battery warning image (%s, ~33s)...\n",
               portrait ? "portrait" : "landscape");
    EPD_13IN3E_DisplayFraimicBin(buf, EPD_13IN3E_FRAIMIC_BIN_BYTES);
    free(buf);
    Log.println("BAT: Low battery warning image refresh complete.");
    return true;
}

bool checkAndHandle() {
    battery::Status bat = battery::read();

    // If external power / charging is detected, reset the display latch
    if (bat.cable_connected || bat.charging) {
        if (lowBatteryDisplayed) {
            Log.println("BAT: External power connected — resetting low battery screen latch.");
            lowBatteryDisplayed = false;
        }
        return false;
    }

    // Low battery condition (percent <= 5% while on battery power)
    if (bat.percent <= 5) {
        // Critical voltage safeguard: under 3.2V, do NOT attempt a 33s high-current refresh
        if (bat.voltage_mv > 0 && bat.voltage_mv < 3200) {
            Log.printf("BAT: Critical voltage (%dmV < 3200mV) — deep sleeping immediately to protect cell.\n",
                       bat.voltage_mv);
            fraimic_api::enterDeepSleepForSeconds(3600ULL * 2);
            return true;
        }

        if (!lowBatteryDisplayed) {
            Log.printf("BAT: Low battery (%d%%, %dmV) detected on battery power! Refreshing warning image...\n",
                       bat.percent, bat.voltage_mv);
            lowBatteryDisplayed = true;
            showScreen();
            Log.println("BAT: Low battery warning displayed — entering deep sleep (2 hours).");
            fraimic_api::enterDeepSleepForSeconds(3600ULL * 2);
            return true;
        } else {
            // Screen was already updated to the warning image. Sleep to prevent draining cell.
            Log.printf("BAT: Depleted battery (%d%%, %dmV) — warning screen already active, sleeping 2 hours.\n",
                       bat.percent, bat.voltage_mv);
            fraimic_api::enterDeepSleepForSeconds(3600ULL * 2);
            return true;
        }
    }

    return false;
}

}  // namespace low_battery
