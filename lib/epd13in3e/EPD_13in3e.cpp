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

// Select exactly one controller. Always force the other CS high so a sticky
// or glitched line cannot leave both ICs listening (which commonly shows up
// as only the master/left half updating).
void CS_MasterOnly(void) {
    DEV_Digital_Write(EPD_CS_S_PIN, 1);
    DEV_Digital_Write(EPD_CS_M_PIN, 0);
}

void CS_SlaveOnly(void) {
    DEV_Digital_Write(EPD_CS_M_PIN, 1);
    DEV_Digital_Write(EPD_CS_S_PIN, 0);
}

// Standard SPI e-paper: DC low = command, DC high = data. Waveshare's 13.3
// sample never toggles DC (it still "works" on some boards if the line
// floats high), but an undefined DC during the long slave data burst is a
// prime suspect for the right-half-stuck failure mode.
void SendCommand(UBYTE cmd) {
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_SPI_WriteByte(cmd);
}

void SendData(UBYTE data) {
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_SPI_WriteByte(data);
}

void SendData2(const UBYTE *buf, uint32_t len) {
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_SPI_Write_nByte(buf, len);
}

void SPI_Send(UBYTE cmd, const UBYTE *buf, UDOUBLE len) {
    SendCommand(cmd);
    SendData2(buf, len);
}

// Match Waveshare's single reset pulse (our older triple-reset could leave
// the slave IC in a worse state after power-off).
void Reset(void) {
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(30);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(30);
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

// Stream one half-panel (300 bytes/row × 1600 rows) to the selected CS.
void StreamHalf(const uint8_t *half, bool master) {
    const UDOUBLE row_bytes = EPD_13IN3E_HALF_BYTES / EPD_13IN3E_HEIGHT;  // 300

    if (master) {
        CS_MasterOnly();
    } else {
        CS_SlaveOnly();
    }
    DEV_Delay_ms(1);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(half + (UDOUBLE)row * row_bytes, row_bytes);
        // delay() yields so WiFi/IDLE can run during the long transfer
        DEV_Delay_ms(1);
    }
    CS_ALL(1);
    DEV_Delay_ms(10);
}

void FillHalf(UBYTE color, bool master) {
    const UDOUBLE row_bytes = EPD_13IN3E_HALF_BYTES / EPD_13IN3E_HEIGHT;
    const UBYTE fill = (color << 4) | color;
    UBYTE row[300];
    for (UDOUBLE i = 0; i < row_bytes && i < 300; i++) row[i] = fill;

    if (master) {
        CS_MasterOnly();
    } else {
        CS_SlaveOnly();
    }
    DEV_Delay_ms(1);
    SendCommand(0x10);
    for (int r = 0; r < EPD_13IN3E_HEIGHT; r++) {
        SendData2(row, row_bytes);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);
    DEV_Delay_ms(10);
}

// Mirror one 300-byte half-row (600 4-bit pixels, high nibble = even local
// column, low nibble = odd — see fraimic_bin_converter's packer) so local
// column x ends up at (599 - x). Byte-reversing alone would leave every
// pixel one column off, since reversing byte order swaps WHICH byte holds a
// pixel but not which nibble within it — so each byte's nibbles must also
// swap.
void MirrorRowInPlace(uint8_t *row, UDOUBLE rowBytes) {
    for (UDOUBLE i = 0; i < rowBytes / 2; i++) {
        uint8_t a = row[i];
        uint8_t b = row[rowBytes - 1 - i];
        row[i] = (uint8_t)((b << 4) | (b >> 4));
        row[rowBytes - 1 - i] = (uint8_t)((a << 4) | (a >> 4));
    }
}

// Rotate one half-panel's 600×1600 sub-image 180° in place: reverse row
// order and mirror each row's pixel order. Combined with swapping which CS
// (master/slave) receives which half — done by the caller — this rotates
// the full 1200×1600 image 180°, correcting a ribbon-at-top mounting.
void RotateHalf180InPlace(uint8_t *half) {
    const UDOUBLE rowBytes = EPD_13IN3E_HALF_BYTES / EPD_13IN3E_HEIGHT;  // 300
    for (int r = 0; r < EPD_13IN3E_HEIGHT / 2; r++) {
        uint8_t *rowA = half + (UDOUBLE)r * rowBytes;
        uint8_t *rowB = half + (UDOUBLE)(EPD_13IN3E_HEIGHT - 1 - r) * rowBytes;
        MirrorRowInPlace(rowA, rowBytes);
        MirrorRowInPlace(rowB, rowBytes);
        for (UDOUBLE i = 0; i < rowBytes; i++) {
            uint8_t tmp = rowA[i];
            rowA[i] = rowB[i];
            rowB[i] = tmp;
        }
    }
}

}  // namespace

void EPD_13IN3E_Init(void) {
    Reset();

    // Ensure both CS lines start deselected and DC is a known level.
    CS_ALL(1);
    DEV_Digital_Write(EPD_DC_PIN, 0);

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

    // Power rail programming is master-only on Waveshare's reference (master
    // IC owns the shared analog supplies for both halves).
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
    EPD_13IN3E_Init();
    FillHalf(color, true);
    FillHalf(color, false);
    TurnOnDisplay();
}

void EPD_13IN3E_Display(const UBYTE *image) {
    // Row-interleaved layout (not Fraimic). Re-pack isn't done here — callers
    // of this path are diagnostic only on this firmware.
    EPD_13IN3E_Init();

    const UDOUBLE width_bytes = EPD_13IN3E_WIDTH / 2;  // 600
    const UDOUBLE half_bytes = width_bytes / 2;         // 300

    CS_MasterOnly();
    DEV_Delay_ms(1);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(image + row * width_bytes, half_bytes);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);
    DEV_Delay_ms(10);

    CS_SlaveOnly();
    DEV_Delay_ms(1);
    SendCommand(0x10);
    for (int row = 0; row < EPD_13IN3E_HEIGHT; row++) {
        SendData2(image + row * width_bytes + half_bytes, half_bytes);
        DEV_Delay_ms(1);
    }
    CS_ALL(1);

    TurnOnDisplay();
}

void EPD_13IN3E_DisplayFraimicBin(uint8_t *bin_data, size_t len) {
    if (len != EPD_13IN3E_FRAIMIC_BIN_BYTES || bin_data == nullptr) return;

    // TurnOnDisplay() ends with POF. Both ICs need a full re-init before the
    // next 0x10 data load.
    EPD_13IN3E_Init();

    uint8_t *left = bin_data;                          // Fraimic cols 0-599
    uint8_t *right = bin_data + EPD_13IN3E_HALF_BYTES;  // Fraimic cols 600-1199

    // This board is mounted ribbon-at-top — 180° from Waveshare's reference
    // ("master = left half, ribbon-at-bottom") — so images arrive rotated
    // 180° unless corrected: rotate each half's content 180° in place, then
    // swap which CS (master/slave) receives which half, since rotating the
    // full image 180° also swaps which physical side each half lands on.
    RotateHalf180InPlace(left);
    RotateHalf180InPlace(right);
    StreamHalf(left, /*master=*/false);
    StreamHalf(right, /*master=*/true);

    TurnOnDisplay();
}

void EPD_13IN3E_Show6Block(void) {
    EPD_13IN3E_Init();

    const UDOUBLE half_bytes = EPD_13IN3E_HALF_BYTES / EPD_13IN3E_HEIGHT;
    const UBYTE colors[6] = {EPD_13IN3E_BLACK, EPD_13IN3E_BLUE,   EPD_13IN3E_GREEN,
                             EPD_13IN3E_RED,   EPD_13IN3E_YELLOW, EPD_13IN3E_WHITE};

    CS_MasterOnly();
    SendCommand(0x10);
    for (int k = 0; k < 6; k++) {
        const UBYTE fill = colors[k] | (colors[k] << 4);
        for (int row = 0; row < EPD_13IN3E_HEIGHT / 6; row++) {
            for (UDOUBLE col = 0; col < half_bytes; col++) SendData(fill);
        }
        DEV_Delay_ms(1);
    }
    CS_ALL(1);
    DEV_Delay_ms(10);

    CS_SlaveOnly();
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

void EPD_13IN3E_ShowHalfColors(void) {
    // Master (Fraimic left / physical left with ribbon at bottom) = RED
    // Slave  (Fraimic right / physical right with ribbon at bottom) = BLUE
    // If only red appears, the slave CS/data path is not reaching the panel.
    EPD_13IN3E_Init();
    FillHalf(EPD_13IN3E_RED, true);
    FillHalf(EPD_13IN3E_BLUE, false);
    TurnOnDisplay();
}

void EPD_13IN3E_Sleep(void) {
    CS_ALL(0);
    SendCommand(0x07);  // DEEP_SLEEP
    SendData(0xA5);
    CS_ALL(1);
}
