#include "es7210.h"

// Public ES7210 register map (as used by Espressif's own open-source codec
// drivers, e.g. esp-adf/esp-sr). See header comment: values below are a
// best-effort port, not verified against this specific board's hardware.
namespace {
constexpr uint8_t REG_RESET = 0x00;
constexpr uint8_t REG_CLOCK_OFF = 0x01;
constexpr uint8_t REG_MAINCLK = 0x02;
constexpr uint8_t REG_MASTER_CLK = 0x03;
constexpr uint8_t REG_LRCK_DIVH = 0x04;
constexpr uint8_t REG_LRCK_DIVL = 0x05;
constexpr uint8_t REG_PWR = 0x06;
constexpr uint8_t REG_SDP_INTERFACE1 = 0x11;
constexpr uint8_t REG_SDP_INTERFACE2 = 0x12;
constexpr uint8_t REG_ADC_AUTOMUTE = 0x13;
constexpr uint8_t REG_ADC34_MUTERANGE = 0x14;
constexpr uint8_t REG_MIC1_GAIN = 0x43;
constexpr uint8_t REG_MIC2_GAIN = 0x44;
constexpr uint8_t REG_MIC12_POWER = 0x4B;
constexpr uint8_t REG_MIC34_POWER = 0x4C;
constexpr uint8_t REG_ANALOG_PDN = 0x4E;
}  // namespace

bool ES7210::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission(true) == 0;
}

uint8_t ES7210::readReg(uint8_t reg) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    Wire.requestFrom(addr_, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void ES7210::reset() {
    writeReg(REG_RESET, 0xFF);
    delay(5);
    writeReg(REG_RESET, 0x41);  // release digital reset, keep analog in reset briefly
}

bool ES7210::begin(uint8_t i2c_addr, uint32_t mclk_hz, uint32_t sample_rate_hz) {
    addr_ = i2c_addr;

    // Probe: chip must ACK its own address before we touch registers.
    Wire.beginTransmission(addr_);
    if (Wire.endTransmission() != 0) return false;

    reset();

    writeReg(REG_CLOCK_OFF, 0x3F);  // gate all internal clocks while configuring
    writeReg(REG_MAINCLK, 0x00);    // MCLK = SCLK/LRCK ratio auto-detected below

    const uint32_t mclk_ratio = (sample_rate_hz > 0) ? (mclk_hz / sample_rate_hz) : 256;
    writeReg(REG_MASTER_CLK, (uint8_t)(mclk_ratio & 0xFF));
    writeReg(REG_LRCK_DIVH, (uint8_t)((mclk_ratio >> 8) & 0xFF));
    writeReg(REG_LRCK_DIVL, (uint8_t)(mclk_ratio & 0xFF));

    writeReg(REG_SDP_INTERFACE1, 0x00);  // I2S, 16-bit, slave mode
    writeReg(REG_SDP_INTERFACE2, 0x02);  // 2-channel (mic1+mic2) TDM slot count

    writeReg(REG_ADC_AUTOMUTE, 0x00);
    writeReg(REG_ADC34_MUTERANGE, 0x00);

    writeReg(REG_MIC12_POWER, 0x00);   // power up mic1/mic2 analog+digital paths
    writeReg(REG_MIC34_POWER, 0xFF);   // mic3/mic4 unused on this board — keep powered down
    writeReg(REG_ANALOG_PDN, 0x00);

    setMicGain(30);  // conservative default; tune once verified on hardware

    writeReg(REG_CLOCK_OFF, 0x00);  // un-gate clocks, start conversion

    return true;
}

void ES7210::setMicGain(uint8_t gain_db) {
    // Datasheet gain steps are coarse (~3dB per LSB up to ~34.5dB); this just
    // clamps into the register's usable range rather than doing exact dB math.
    uint8_t reg_val = gain_db > 37 ? 37 : gain_db;
    writeReg(REG_MIC1_GAIN, reg_val);
    writeReg(REG_MIC2_GAIN, reg_val);
}
