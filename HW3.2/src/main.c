#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define POT_CHANNEL  ADC_CHANNEL_3      // GPIO4 на ESP32-S3
#define POT_ATTEN    ADC_ATTEN_DB_11    // ~0..3.1 В
#define POT_BITWIDTH ADC_BITWIDTH_12    // 0..4095
#define VREF_MV      3300.0f
#define ADC_MAX      4095.0f
#define AVG_N        16
#define PERIOD_MS    100

static const char *TAG = "ADC";

void app_main(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = POT_ATTEN,
        .bitwidth = POT_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, POT_CHANNEL, &ch_cfg));

    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = POT_CHANNEL,
        .atten    = POT_ATTEN,
        .bitwidth = POT_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
        ESP_LOGE(TAG, "Calibration not supported / eFuse empty");
        cali = NULL;
    }

    printf("\nADC calibration | ESP32-S3 | 12 bit | atten 11 dB | Vref 3300 mV\n");
    printf("RAW    U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("--------------------------------------------\n");

    while (1) {
        int raw_sum = 0;
        for (int i = 0; i < AVG_N; i++) {
            int r = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(adc, POT_CHANNEL, &r));
            raw_sum += r;
        }
        int raw = raw_sum / AVG_N;

        float u_man = raw * VREF_MV / ADC_MAX;   // U = RAW * Vref / 4095

        int u_cali = 0;
        if (cali) {
            adc_cali_raw_to_voltage(cali, raw, &u_cali);
        }

        float err = (u_cali > 1) ? fabsf(u_man - (float)u_cali) / (float)u_cali * 100.0f : 0.0f;

        printf("%4d   %10.1f   %10d   %8.2f\n", raw, u_man, u_cali, err);
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
