#include "EPD_13in3e.h"

namespace {

constexpr uint8_t PSR = 0x00;
constexpr uint8_t PWR_epd = 0x01;
constexpr uint8_t POF = 0x02;
constexpr uint8_t DRF = 0x12;
constexpr uint8_t CDI = 0x50;
constexpr uint8_t TCON = 0x60;
constexpr uint8_t TRES = 0x61;
constexpr uint8_t AN_TM = 0x74;
constexpr uint8_t AGID = 0x86;
constexpr uint8_t BUCK_BOOST_VDDN = 0xB0;
constexpr uint8_t TFT_VCOM_POWER = 0xB1;
constexpr uint8_t EN_BUF = 0xB6;
constexpr uint8_t BOOST_VDDP_EN = 0xB7;
constexpr uint8_t BTST_P = 0x06;
constexpr uint8_t BTST_N = 0x05;
constexpr uint8_t CCSET = 0xE0;
constexpr uint8_t PWS = 0xE3;
constexpr uint8_t CMD66 = 0xF0;

const UBYTE PSR_V[2] = {0xDF, 0x69};
const UBYTE PWR_V[6] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
const UBYTE POF_V[1] = {0x00};
const UBYTE DRF_V[1] = {0x00};
const UBYTE CDI_V[1] = {0xF7};
const UBYTE TCON_V[2] = {0x03, 0x03};
const UBYTE TRES_V[4] = {0x04, 0xB0, 0x03, 0x20};
const UBYTE CMD66_V[6] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
const UBYTE EN_BUF_V[1] = {0x07};
const UBYTE CCSET_V[1] = {0x01};
const UBYTE PWS_V[1] = {0x22};
const UBYTE AN_TM_V[9] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
const UBYTE AGID_V[1] = {0x10};
const UBYTE BTST_P_V[2] = {0xE8, 0x28};
const UBYTE BOOST_VDDP_EN_V[1] = {0x01};
const UBYTE BTST_N_V[2] = {0xE8, 0x28};
const UBYTE BUCK_BOOST_VDDN_V[1] = {0x01};
const UBYTE TFT_VCOM_POWER_V[1] = {0x02};

void CS_ALL(int value) {
    DEV_Digital_Write(EPD_CS_M_PIN, value);
    DEV_Digital_Write(EPD_CS_S_PIN, value);
}

void SendCommand(UBYTE cmd) { DEV_SPI_WriteByte(cmd); }
void SendData(UBYTE data) { DEV_SPI_WriteByte(data); }
void SendData2(const UBYTE *buf, uint32_t len) { DEV_SPI_Write_nByte(buf, len); }

void SPI_Send(UBYTE cmd, const UBYTE *buf, UDOUBLE len) {
    DEV_SPI_WriteByte(cmd);
    DEV_SPI_Write_nByte(buf, len);
}

void Reset(void) {
    for (int i = 0; i < 3; i++) {
        DEV_Digital_Write(EPD_RST_PIN, 1);
        DEV_Delay_ms(30);
        DEV_Digital_Write(EPD_RST_PIN, 0);
        DEV_Delay_ms(30);
    }
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(30);
}

void ReadBusyH(void) {
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {  // LOW: busy, HIGH: idle
        DEV_Delay_ms(10);
    }
    DEV_Delay_ms(20);
}

void TurnOnDisplay(void) {
    CS_ALL(0);
    SendCommand(0x04);  // POWER_ON
    CS_ALL(1);
    ReadBusyH();

    DEV_Delay_ms(50);
    CS_ALL(0);
    SPI_Send(DRF, DRF_V, sizeof(DRF_V));
    CS_ALL(1);
    ReadBusyH();

    CS_ALL(0);
    SPI_Send(POF, POF_V, sizeof(POF_V));
    CS_ALL(1);
}

}  // namespace

void EPD_13IN3E_Init(void) {
    Reset();

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(AN_TM, AN_TM_V, sizeof(AN_TM_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(CMD66, CMD66_V, sizeof(CMD66_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(PSR, PSR_V, sizeof(PSR_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(CDI, CDI_V, sizeof(CDI_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(TCON, TCON_V, sizeof(TCON_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(AGID, AGID_V, sizeof(AGID_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(PWS, PWS_V, sizeof(PWS_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(CCSET, CCSET_V, sizeof(CCSET_V));
    CS_ALL(1);

    CS_ALL(0);
    SPI_Send(TRES, TRES_V, sizeof(TRES_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(PWR_epd, PWR_V, sizeof(PWR_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(BTST_P, BTST_P_V, sizeof(BTST_P_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(BTST_N, BTST_N_V, sizeof(BTST_N_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V));
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SPI_Send(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V));
    CS_ALL(1);
}

void EPD_13IN3E_Clear(UBYTE color) {
    const UDOUBLE width_bytes = EPD_13IN3E_WIDTH / 2;  // bytes per full row (both halves)
    const UBYTE fill = (color << 4) | color;
    UBYTE buf[width_bytes / 2];
    for (UDOUBLE j = 0; j < width_bytes / 2; j++) buf[j] = fill;

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(buf, width_bytes / 2);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_S_PIN, 0);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(buf, width_bytes / 2);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    TurnOnDisplay();
}

void EPD_13IN3E_Display(const UBYTE *image) {
    const UDOUBLE width_bytes = EPD_13IN3E_WIDTH / 2;   // 600: full-row stride in `image`
    const UDOUBLE half_bytes = width_bytes / 2;          // 300: bytes per half-row

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(image + row * width_bytes, half_bytes);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_S_PIN, 0);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(image + row * width_bytes + half_bytes, half_bytes);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    TurnOnDisplay();
}

void EPD_13IN3E_DisplayFraimicBin(const uint8_t *bin_data, size_t len) {
    if (len != EPD_13IN3E_FRAIMIC_BIN_BYTES) return;

    const uint8_t *left = bin_data;                          // all master rows, row-major
    const uint8_t *right = bin_data + EPD_13IN3E_HALF_BYTES;  // all slave rows, row-major
    const UDOUBLE half_bytes = EPD_13IN3E_HALF_BYTES / EPD_13IN3E_HEIGHT;  // 300 bytes/row

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SendCommand(0x10);
    SendData2(left, EPD_13IN3E_HALF_BYTES);
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_S_PIN, 0);
    SendCommand(0x10);
    SendData2(right, EPD_13IN3E_HALF_BYTES);
    CS_ALL(1);

    (void)half_bytes;  // rows are already contiguous per half; no per-row loop needed
    TurnOnDisplay();
}

void EPD_13IN3E_Show6Block(void) {
    const UDOUBLE width_bytes = EPD_13IN3E_WIDTH / 2;
    const UDOUBLE half_bytes = width_bytes / 2;
    const UBYTE colors[6] = {EPD_13IN3E_BLACK, EPD_13IN3E_BLUE,   EPD_13IN3E_GREEN,
                             EPD_13IN3E_RED,   EPD_13IN3E_YELLOW, EPD_13IN3E_WHITE};

    DEV_Digital_Write(EPD_CS_M_PIN, 0);
    SendCommand(0x10);
    for (int k = 0; k < 6; k++) {
        const UBYTE fill = colors[k] | (colors[k] << 4);
        for (int row = 0; row < EPD_13IN3E_HEIGHT / 6; row++) {
            for (UDOUBLE col = 0; col < half_bytes; col++) SendData(fill);
        }
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    DEV_Digital_Write(EPD_CS_S_PIN, 0);
    SendCommand(0x10);
    for (int k = 0; k < 6; k++) {
        const UBYTE fill = colors[k] | (colors[k] << 4);
        for (int row = 0; row < EPD_13IN3E_HEIGHT / 6; row++) {
            for (UDOUBLE col = 0; col < half_bytes; col++) SendData(fill);
        }
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    TurnOnDisplay();
}

void EPD_13IN3E_Sleep(void) {
    CS_ALL(0);
    SendCommand(0x07);  // DEEP_SLEEP
    SendData(0xA5);
    CS_ALL(1);
}
