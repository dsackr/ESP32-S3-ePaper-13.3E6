#include "battery.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

#include "pins.h"

// Adapted from Waveshare's 01_ADC_Test example (adc_bsp.cpp) — see
// /NOTICE.md. That example's raw-voltage formula assumes a resistor divider
// ahead of the ADC pin; VERIFY kDividerRatio against a multimeter on real
// hardware before trusting the reported percentage — the vendor sample
// itself used two different, inconsistent ratios (2x and 3x) between its
// calibrated and uncalibrated code paths.
namespace battery {

namespace {

constexpr float kDividerRatio = 2.0f;
constexpr float kEmptyVoltage = 3.3f;   // 0% — single-cell Li-ion/LiPo
constexpr float kFullVoltage = 4.2f;    // 100%

adc_oneshot_unit_handle_t adc1Handle = nullptr;
adc_cali_handle_t caliHandle = nullptr;
bool caliOk = false;

int voltageToPercent(float volts) {
    if (volts <= kEmptyVoltage) return 0;
    if (volts >= kFullVoltage) return 100;
    return (int)((volts - kEmptyVoltage) / (kFullVoltage - kEmptyVoltage) * 100.0f);
}

}  // namespace

void init() {
    adc_oneshot_unit_init_cfg_t initCfg = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&initCfg, &adc1Handle);

    adc_oneshot_chan_cfg_t chanCfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc1Handle, PIN_BATTERY_ADC_CHANNEL, &chanCfg);

    adc_cali_curve_fitting_config_t caliCfg = {
        .unit_id = ADC_UNIT_1,
        .chan = PIN_BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    caliOk = adc_cali_create_scheme_curve_fitting(&caliCfg, &caliHandle) == ESP_OK;
}

Status read() {
    Status status{.percent = 0, .voltage_mv = 0, .charging = false, .cable_connected = false};
    if (!adc1Handle) return status;

    int raw = 0;
    if (adc_oneshot_read(adc1Handle, PIN_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) return status;

    float pin_volts;
    if (caliOk) {
        int mv = 0;
        adc_cali_raw_to_voltage(caliHandle, raw, &mv);
        pin_volts = mv / 1000.0f;
    } else {
        pin_volts = raw * 3.3f / 4096.0f;
    }

    float batt_volts = pin_volts * kDividerRatio;
    status.voltage_mv = (int)(batt_volts * 1000);
    status.percent = voltageToPercent(batt_volts);

    // No confirmed charge-status line from the ETA6098 is broken out on this
    // board per the schematic reviewed so far — cable/charging state isn't
    // derivable from the ADC alone. Revisit if a STAT/CHRG pin turns up.
    status.charging = false;
    status.cable_connected = false;
    return status;
}

}  // namespace battery
