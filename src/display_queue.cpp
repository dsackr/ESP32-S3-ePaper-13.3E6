#include "display_queue.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include "EPD_13in3e.h"
#include "remote_log.h"

namespace display_queue {

namespace {

volatile bool busyFlag = false;
// Own a private PSRAM copy so HTTP/pull cannot overwrite the buffer while
// the dual-IC SPI transfer is still streaming (a mid-paint overwrite of the
// second half is a classic "right half stuck on old content" failure).
uint8_t *paintBuf = nullptr;
SemaphoreHandle_t wakeSem = nullptr;

enum class Job : uint8_t { None, FraimicBin, HalfColors };
volatile Job pendingJob = Job::None;

void taskFn(void *) {
    for (;;) {
        xSemaphoreTake(wakeSem, portMAX_DELAY);
        Job job = pendingJob;
        pendingJob = Job::None;

        uint32_t startMs = millis();
        if (job == Job::HalfColors) {
            Log.println("DISPLAY: half-color test (master=RED slave=BLUE)");
            EPD_13IN3E_ShowHalfColors();
        } else if (job == Job::FraimicBin && paintBuf != nullptr) {
            Log.printf("DISPLAY: refresh start, %u bytes, heap=%u\n",
                        (unsigned)EPD_13IN3E_FRAIMIC_BIN_BYTES, ESP.getFreeHeap());
            EPD_13IN3E_DisplayFraimicBin(paintBuf, EPD_13IN3E_FRAIMIC_BIN_BYTES);
        } else {
            Log.println("DISPLAY: empty job, skip");
        }
        Log.printf("DISPLAY: refresh done, took %ums\n", (unsigned)(millis() - startMs));
        busyFlag = false;
    }
}

}  // namespace

void begin() {
    paintBuf = (uint8_t *)heap_caps_malloc(EPD_13IN3E_FRAIMIC_BIN_BYTES, MALLOC_CAP_SPIRAM);
    if (!paintBuf) {
        Log.println("FATAL: display_queue paint buffer alloc failed");
    }
    wakeSem = xSemaphoreCreateBinary();
    // Priority 2 so bit-bang SPI is less likely to be interrupted mid-row by
    // the async HTTP worker (priority 1 by default on many Arduino builds).
    xTaskCreate(taskFn, "display_task", 8192, nullptr, 2, nullptr);
}

bool busy() { return busyFlag; }

bool requestDisplay(const uint8_t *bin_data, size_t len) {
    if (busyFlag || paintBuf == nullptr || bin_data == nullptr) return false;
    if (len != EPD_13IN3E_FRAIMIC_BIN_BYTES) return false;
    memcpy(paintBuf, bin_data, len);
    pendingJob = Job::FraimicBin;
    busyFlag = true;
    xSemaphoreGive(wakeSem);
    return true;
}

bool requestHalfColorTest() {
    if (busyFlag) return false;
    pendingJob = Job::HalfColors;
    busyFlag = true;
    xSemaphoreGive(wakeSem);
    return true;
}

}  // namespace display_queue
