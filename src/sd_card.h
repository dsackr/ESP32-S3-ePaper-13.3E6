#pragma once

#include <cstdint>

namespace sd_card {

// Mounts the TF card at /sdcard (FAT32). Returns false if no card is present
// or mounting fails — callers should treat SD as optional, not fatal.
bool init();
bool isMounted();
uint64_t sizeBytes();

}  // namespace sd_card
