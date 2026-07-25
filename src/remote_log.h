#pragma once

#include <Arduino.h>

// Drop-in replacement for Serial.print/println/printf: everything written
// still goes out the hardware UART (so USB serial monitoring keeps working
// exactly as before), but a copy is also kept in a small RAM ring buffer so
// the /logs web page can show live device logs over WiFi with no cable and
// no extra app. RAM-only — the buffer resets on reboot, nothing is
// persisted to SD.
class RemoteLog : public Print {
public:
    void begin(unsigned long baud);
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    // Snapshot of the buffered log text, oldest line first.
    String snapshot();
};

extern RemoteLog Log;
