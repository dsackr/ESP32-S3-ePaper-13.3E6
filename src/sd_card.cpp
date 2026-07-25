#include "sd_card.h"

#include <SD_MMC.h>

#include "pins.h"

namespace sd_card {

namespace {
bool mounted = false;
}

bool init() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    mounted = SD_MMC.begin("/sdcard", false /* 4-bit mode */);
    return mounted;
}

bool isMounted() { return mounted; }

uint64_t sizeBytes() { return mounted ? SD_MMC.cardSize() : 0; }

}  // namespace sd_card
