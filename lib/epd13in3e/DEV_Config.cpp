#include "DEV_Config.h"

static void GPIO_Config(void) {
    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_PWR_PIN, OUTPUT);
    pinMode(EPD_SCK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_M_PIN, OUTPUT);
    pinMode(EPD_CS_S_PIN, OUTPUT);

    digitalWrite(EPD_CS_M_PIN, HIGH);
    digitalWrite(EPD_CS_S_PIN, HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);
    digitalWrite(EPD_PWR_PIN, HIGH);
}

void GPIO_Mode(UWORD pin, UWORD mode) {
    pinMode(pin, mode == 0 ? INPUT : OUTPUT);
}

UBYTE DEV_Module_Init(void) {
    GPIO_Config();
    return 0;
}

// Bit-banged SPI (no MISO — this panel's read path reuses MOSI in input
// mode). Matches Waveshare's reference driver; the full-panel refresh is
// dominated by the controller's ~19s internal update time, not this loop.
void DEV_SPI_WriteByte(UBYTE data) {
    for (int i = 0; i < 8; i++) {
        digitalWrite(EPD_MOSI_PIN, (data & 0x80) ? HIGH : LOW);
        data <<= 1;
        digitalWrite(EPD_SCK_PIN, HIGH);
        digitalWrite(EPD_SCK_PIN, LOW);
    }
}

void DEV_SPI_Write_nByte(const UBYTE *data, UDOUBLE len) {
    for (UDOUBLE i = 0; i < len; i++) {
        DEV_SPI_WriteByte(data[i]);
    }
}

void DEV_Module_Exit(void) {
    digitalWrite(EPD_PWR_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
}
