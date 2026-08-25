#include "battery.h"

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

#include "pins.h"
#include "remote_log.h"

// Battery sense: GPIO8 / ADC1_CH7 through an onboard resistor divider.
// Charge status: GPIO38 (Waveshare pin map "CHARGE STATE" / ETA6098 STAT).
//
// Voltage scale matches Waveshare's 01_ADC_Test calibrated path:
//   battery_volts = adc_pin_volts * 3.0
// (their uncalibrated demo used *2 — inconsistent; calibrated *3 is what we
// follow). Percent is a simple single-cell Li-ion open-circuit estimate.
namespace battery {

namespace {

// Waveshare 01_ADC_Test (calibrated): value = pin_mV * 3 / 1000.
constexpr float kDividerRatio = 3.0f;
constexpr float kEmptyVoltage = 3.30f;
constexpr float kFullVoltage = 4.20f;

// Waveshare pin map: GPIO38 = CHARGE STATE (ETA6098 STAT, typically
// open-drain active-low while charging).
constexpr gpio_num_t kChargeStateGpio = GPIO_NUM_38;

constexpr int kSampleCount = 16;

adc_oneshot_unit_handle_t adc1Handle = nullptr;
adc_cali_handle_t caliHandle = nullptr;
bool caliOk = false;

int voltageToPercent(float volts) {
    if (volts <= kEmptyVoltage) return 0;
    if (volts >= kFullVoltage) return 100;
    return (int)((volts - kEmptyVoltage) / (kFullVoltage - kEmptyVoltage) * 100.0f + 0.5f);
}

}  // namespace

void init() {
    adc_oneshot_unit_init_cfg_t initCfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&initCfg, &adc1Handle) != ESP_OK) {
        Log.println("BAT: adc_oneshot_new_unit failed");
        adc1Handle = nullptr;
        return;
    }

    adc_oneshot_chan_cfg_t chanCfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(adc1Handle, PIN_BATTERY_ADC_CHANNEL, &chanCfg) != ESP_OK) {
        Log.println("BAT: config channel failed");
    }

    // Match Waveshare 01_ADC_Test calibration setup (unit + atten + width).
    adc_cali_curve_fitting_config_t caliCfg = {
        .unit_id = ADC_UNIT_1,
        .chan = PIN_BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    caliOk = adc_cali_create_scheme_curve_fitting(&caliCfg, &caliHandle) == ESP_OK;
    if (!caliOk) {
        Log.println("BAT: curve-fitting cali unavailable — using linear 3.3V scale");
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << kChargeStateGpio;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;  // STAT is usually OD; idle high = not charging
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    Log.printf("BAT: ADC1_CH%u (GPIO8) ratio=%.1f cali=%d CHARGE=GPIO%d\n",
                (unsigned)PIN_BATTERY_ADC_CHANNEL, kDividerRatio, caliOk ? 1 : 0,
                (int)kChargeStateGpio);
}

Status read() {
    Status status{.percent = 0,
                  .voltage_mv = 0,
                  .pin_mv = 0,
                  .adc_raw = 0,
                  .charging = false,
                  .cable_connected = false};
    if (!adc1Handle) return status;

    int rawSum = 0;
    int ok = 0;
    for (int i = 0; i < kSampleCount; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc1Handle, PIN_BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
            rawSum += raw;
            ok++;
        }
    }
    if (ok == 0) return status;

    int rawAvg = rawSum / ok;
    status.adc_raw = rawAvg;

    int pin_mv = 0;
    if (caliOk) {
        if (adc_cali_raw_to_voltage(caliHandle, rawAvg, &pin_mv) != ESP_OK) {
            pin_mv = (int)(rawAvg * 3300.0f / 4095.0f);
        }
    } else {
        pin_mv = (int)(rawAvg * 3300.0f / 4095.0f);
    }
    status.pin_mv = pin_mv;

    float batt_volts = (pin_mv / 1000.0f) * kDividerRatio;
    status.voltage_mv = (int)(batt_volts * 1000.0f + 0.5f);
    status.percent = voltageToPercent(batt_volts);

    // ETA6098 STAT: active-low while charging (with pull-up).
    // cable_connected: STAT low (charging) OR cell voltage above ~3.0V while
    // USB may be present — we only know "charging" reliably from STAT.
    int stat = gpio_get_level(kChargeStateGpio);
    status.charging = (stat == 0);
    status.cable_connected = status.charging;

    return status;
}

}  // namespace battery
