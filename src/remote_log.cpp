#include "remote_log.h"

// Fixed-size RAM ring buffer holding the most recent log text. 6KB is
// plenty for a screenful+ of history; this board has 16MB of PSRAM, so
// even keeping it in internal SRAM costs nothing meaningful.
static const size_t LOG_BUF_SIZE = 6144;
static char logBuf[LOG_BUF_SIZE];
static size_t logWritePos = 0;  // next index to write
static bool logWrapped = false;

RemoteLog Log;

void RemoteLog::begin(unsigned long baud) { Serial.begin(baud); }

size_t RemoteLog::write(uint8_t c) { return write(&c, 1); }

size_t RemoteLog::write(const uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        logBuf[logWritePos] = (char)buffer[i];
        logWritePos++;
        if (logWritePos >= LOG_BUF_SIZE) {
            logWritePos = 0;
            logWrapped = true;
        }
    }
    return Serial.write(buffer, size);
}

String RemoteLog::snapshot() {
    // Built one char at a time with the universally-available
    // String::operator+=(char) rather than a length-based concat(), since
    // that overload isn't guaranteed across Arduino core String
    // implementations. Runs at most once per second (triggered by the
    // /logs page poll), so the extra loop overhead is negligible.
    String out;
    out.reserve(LOG_BUF_SIZE + 1);
    if (logWrapped) {
        for (size_t i = logWritePos; i < LOG_BUF_SIZE; i++) out += logBuf[i];
    }
    for (size_t i = 0; i < logWritePos; i++) out += logBuf[i];
    return out;
}
