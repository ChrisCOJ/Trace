#include "battery_monitor.h"

#include "esp_log.h"
#include "esp_err.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *BATT_TAG = "BATT";

// ===== Board-specific config =====
// Waveshare battery sense path:
// GPIO1 <- divider from battery
#define BATTERY_ADC_UNIT          ADC_UNIT_1
#define BATTERY_ADC_CHANNEL       ADC_CHANNEL_0   // GPIO1 on ESP32-S3
#define BATTERY_ADC_ATTEN         ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH      ADC_BITWIDTH_12

// Divider ratio: 200k top / 100k bottom => Vadc = Vbat * (100 / 300) = Vbat / 3
#define BATTERY_DIVIDER_SCALE     3.0f

// Smoothing factor for filtered voltage
// closer to 1.0 = slower, steadier
#define BATTERY_FILTER_ALPHA      0.85f

// Number of raw samples per update
#define BATTERY_SAMPLE_COUNT      32

// 4-bar thresholds
#define VBAT_BAR_4                3.95f
#define VBAT_BAR_3                3.80f
#define VBAT_BAR_2                3.65f
#define VBAT_BAR_1                3.50f

// Hysteresis in volts
#define BAR_HYSTERESIS            0.04f

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_enabled = false;

static battery_monitor_state s_state = {
    .voltage = 0.0f,
    .bars = 0,
    .initialized = false
};

static bool battery_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    esp_err_t err;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
    if (err == ESP_OK) {
        calibrated = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, out_handle);
    if (err == ESP_OK) {
        calibrated = true;
    }
#else
    (void)unit;
    (void)atten;
    (void)out_handle;
#endif

    return calibrated;
}

static int battery_adc_read_raw_avg(void) {
    int raw = 0;
    uint32_t sum = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw));
        sum += (uint32_t)raw;
    }

    return (int)(sum / BATTERY_SAMPLE_COUNT);
}

static float battery_raw_to_vbat(int raw) {
    float vadc_volts;

    if (s_cali_enabled) {
        int mv = 0;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali_handle, raw, &mv));
        vadc_volts = mv / 1000.0f;
    } else {
        // fallback approximation if calibration is unavailable
        vadc_volts = ((float)raw / 4095.0f) * 3.3f;
    }

    return vadc_volts * BATTERY_DIVIDER_SCALE;
}

static uint8_t battery_bars_from_voltage_hysteretic(float vbat, uint8_t prev_bars) {
    switch (prev_bars) {
        case 4:
            return (vbat < (VBAT_BAR_4 - BAR_HYSTERESIS)) ? 3 : 4;

        case 3:
            if (vbat >= (VBAT_BAR_4 + BAR_HYSTERESIS)) return 4;
            if (vbat <  (VBAT_BAR_3 - BAR_HYSTERESIS)) return 2;
            return 3;

        case 2:
            if (vbat >= (VBAT_BAR_3 + BAR_HYSTERESIS)) return 3;
            if (vbat <  (VBAT_BAR_2 - BAR_HYSTERESIS)) return 1;
            return 2;

        case 1:
            if (vbat >= (VBAT_BAR_2 + BAR_HYSTERESIS)) return 2;
            if (vbat <  (VBAT_BAR_1 - BAR_HYSTERESIS)) return 0;
            return 1;

        case 0:
        default:
            if (vbat >= (VBAT_BAR_1 + BAR_HYSTERESIS)) return 1;
            return 0;
    }
}

void battery_monitor_init(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg));

    s_cali_enabled = battery_adc_calibration_init(BATTERY_ADC_UNIT, BATTERY_ADC_ATTEN, &s_cali_handle);

    if (s_cali_enabled) {
        ESP_LOGI(BATT_TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(BATT_TAG, "ADC calibration unavailable, using approximate conversion");
    }

    // Prime filter with first reading
    int raw = battery_adc_read_raw_avg();
    float vbat = battery_raw_to_vbat(raw);

    s_state.voltage = vbat;
    s_state.bars = battery_bars_from_voltage_hysteretic(vbat, 4);
    s_state.initialized = true;

    ESP_LOGI(BATT_TAG, "Battery monitor initialized: raw=%d, vbat=%.3fV, bars=%u",
             raw, s_state.voltage, s_state.bars);
}

void battery_monitor_update(void) {
    if (!s_state.initialized) {
        battery_monitor_init();
        return;
    }

    int raw = battery_adc_read_raw_avg();
    float vbat_now = battery_raw_to_vbat(raw);

    // Exponential smoothing
    s_state.voltage = (BATTERY_FILTER_ALPHA * s_state.voltage) +
                      ((1.0f - BATTERY_FILTER_ALPHA) * vbat_now);

    s_state.bars = battery_bars_from_voltage_hysteretic(s_state.voltage, s_state.bars);

    ESP_LOGD(BATT_TAG, "raw=%d instant=%.3fV filtered=%.3fV bars=%u",
             raw, vbat_now, s_state.voltage, s_state.bars);
}

float battery_monitor_get_voltage(void) {
    return s_state.voltage;
}

uint8_t battery_monitor_get_bars(void) {
    return s_state.bars;
}

battery_monitor_state battery_monitor_get_state(void) {
    return s_state;
}