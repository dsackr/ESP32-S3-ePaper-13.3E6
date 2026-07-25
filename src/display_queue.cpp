#include "display_queue.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "EPD_13in3e.h"
#include "remote_log.h"

namespace display_queue {

namespace {

volatile bool busyFlag = false;
const uint8_t *pendingData = nullptr;
size_t pendingLen = 0;
SemaphoreHandle_t wakeSem = nullptr;

void taskFn(void *) {
    for (;;) {
        xSemaphoreTake(wakeSem, portMAX_DELAY);
        Log.printf("DISPLAY: refresh start, %u bytes, heap=%u\n", (unsigned)pendingLen, ESP.getFreeHeap());
        uint32_t startMs = millis();
        EPD_13IN3E_DisplayFraimicBin(pendingData, pendingLen);
        Log.printf("DISPLAY: refresh done, took %ums\n", (unsigned)(millis() - startMs));
        busyFlag = false;
    }
}

}  // namespace

void begin() {
    wakeSem = xSemaphoreCreateBinary();
    xTaskCreate(taskFn, "display_task", 8192, nullptr, 1, nullptr);
}

bool busy() { return busyFlag; }

bool requestDisplay(const uint8_t *bin_data, size_t len) {
    if (busyFlag) return false;
    pendingData = bin_data;
    pendingLen = len;
    busyFlag = true;
    xSemaphoreGive(wakeSem);
    return true;
}

}  // namespace display_queue
