/*
 * Модуль 3.2 — Калібрування даних з ADC
 * ESP32-S3 + потенціометр 10 кОм (подільник напруги) на GPIO4 (ADC1_CH3).
 *
 * Кожні 100 мс програма:
 *   1) зчитує RAW значення ADC (усереднене по 16 відліках);
 *   2) обчислює напругу вручну за формулою U = RAW * Vref / 4095;
 *   3) отримує напругу з каліброваного ADC (curve fitting, дані з eFuse);
 *   4) рахує похибку (%) між ручною та каліброваною напругою;
 *   5) виводить рядок таблиці в серійну консоль (115200).
 */

#include <stdio.h>
#include <math.h>                     // fabsf()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"            // vTaskDelay()
#include "esp_log.h"                  // ESP_LOGE()
#include "esp_adc/adc_oneshot.h"      // драйвер ADC у режимі oneshot
#include "esp_adc/adc_cali.h"         // калібрування ADC
#include "esp_adc/adc_cali_scheme.h"  // схема калібрування curve fitting

// ---------------- Налаштування ----------------
#define POT_CHANNEL  ADC_CHANNEL_3      // GPIO4 на ESP32-S3 = ADC1, канал 3
#define POT_ATTEN    ADC_ATTEN_DB_11    // атенюація 11 dB: вхідний діапазон ~0..3.1 В
#define POT_BITWIDTH ADC_BITWIDTH_12    // розрядність 12 біт: RAW 0..4095
#define VREF_MV      3300.0f            // опорна напруга для ручної формули, мВ
#define ADC_MAX      4095.0f            // максимальне RAW значення для 12 біт
#define AVG_N        16                 // кількість відліків для усереднення (зменшує шум)
#define PERIOD_MS    100                // період вимірювань, мс

static const char *TAG = "ADC";         // тег для логів

void app_main(void)
{
    // ---------- 1. Ініціалізація блоку ADC1 ----------
    adc_oneshot_unit_handle_t adc;                 // дескриптор ADC
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,                     // використовуємо ADC1 (не конфліктує з Wi-Fi)
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    // ---------- 2. Налаштування каналу (атенюація та розрядність) ----------
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = POT_ATTEN,
        .bitwidth = POT_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, POT_CHANNEL, &ch_cfg));

    // ---------- 3. Створення схеми калібрування ----------
    // Curve fitting використовує заводські калібрувальні дані з eFuse
    // і перетворює RAW у реальну напругу в мВ з урахуванням нелінійності ADC.
    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = POT_CHANNEL,
        .atten    = POT_ATTEN,
        .bitwidth = POT_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
        // Якщо в eFuse немає даних — калібрування недоступне, U_cali буде 0
        ESP_LOGE(TAG, "Calibration not supported / eFuse empty");
        cali = NULL;
    }

    // ---------- 4. Заголовок таблиці ----------
    printf("\nADC calibration | ESP32-S3 | 12 bit | atten 11 dB | Vref 3300 mV\n");
    printf("RAW    U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("--------------------------------------------\n");

    // ---------- 5. Основний цикл вимірювань ----------
    while (1) {
        // 5.1 Зчитуємо AVG_N відліків і усереднюємо RAW
        int raw_sum = 0;
        for (int i = 0; i < AVG_N; i++) {
            int r = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(adc, POT_CHANNEL, &r));
            raw_sum += r;
        }
        int raw = raw_sum / AVG_N;

        // 5.2 Ручне обчислення напруги: U = RAW * Vref / 4095 (ідеальна лінійна модель)
        float u_man = raw * VREF_MV / ADC_MAX;

        // 5.3 Калібрована напруга (мВ) через драйвер ESP-IDF
        int u_cali = 0;
        if (cali) {
            adc_cali_raw_to_voltage(cali, raw, &u_cali);
        }

        // 5.4 Відносна похибка ручного обчислення відносно каліброваного, %
        //     (при U_cali ≈ 0 не ділимо на нуль)
        float err = (u_cali > 1) ? fabsf(u_man - (float)u_cali) / (float)u_cali * 100.0f : 0.0f;

        // 5.5 Виводимо рядок таблиці
        printf("%4d   %10.1f   %10d   %8.2f\n", raw, u_man, u_cali, err);

        // 5.6 Пауза до наступного вимірювання (~100 мс)
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
