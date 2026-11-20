/*
 * Модуль 3.5 — Керування сервоприводом за допомогою PWM
 * Потенціометр (GPIO4) керує валом SG90 (GPIO7) у пропорції 1:1:
 *   поворот ручки на 1° = поворот валу на 1°.
 * Потенціометр має ~270° ходу, серво — 180°, тому використовується
 * лише спільна частина діапазону (0..180°), решта ходу ручки обрізається.
 * Кут від крайнього лівого положення логується в консоль.
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

// ---------- Піни ----------
#define POT_CH          ADC_CHANNEL_3      // GPIO4 = ADC1_CH3
#define SERVO_GPIO      7

// ---------- Потенціометр ----------
#define ADC_MAX         4095               // 12 біт
#define POT_RANGE_DEG   270                // механічний хід потенціометра, °
#define AVG_N           16                 // усереднення від шуму

// ---------- Сервопривід ----------
#define SERVO_RANGE_DEG 180                // хід валу SG90, °
#define SERVO_MIN_US    500                // імпульс для 0°
#define SERVO_MAX_US    2500               // імпульс для 180°
#define SERVO_FREQ      50                 // 50 Гц
#define SERVO_PERIOD_US 20000              // період 20 мс
#define SERVO_RES       LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX  16383              // 2^14 - 1

// ---------- Таймінги ----------
#define PERIOD_MS       20                 // оновлення 50 разів на секунду
#define LOG_EVERY       10                 // лог кожні 10 циклів = 200 мс

// RAW, що відповідає 180° ходу ручки: межа спільного діапазону
#define RAW_LIMIT       (ADC_MAX * SERVO_RANGE_DEG / POT_RANGE_DEG)   // ≈ 2730

static adc_oneshot_unit_handle_t adc;

// Усереднене читання потенціометра (0..4095)
static uint32_t read_pot(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < AVG_N; i++) {
        int r = 0;
        adc_oneshot_read(adc, POT_CH, &r);
        sum += (uint32_t)r;
    }
    return sum / AVG_N;
}

// Налаштування ШІМ для серво: таймер0, канал0, 50 Гц, 14 біт
static void servo_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = SERVO_RES, .freq_hz = SERVO_FREQ, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .gpio_num = SERVO_GPIO, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

// Поставити серво на кут (0..180°)
static void servo_set_deg(uint32_t deg)
{
    uint32_t us   = SERVO_MIN_US + deg * (SERVO_MAX_US - SERVO_MIN_US) / SERVO_RANGE_DEG;
    uint32_t duty = us * SERVO_DUTY_MAX / SERVO_PERIOD_US;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void app_main(void)
{
    // ADC1, GPIO4: 12 біт, атенюація 11 dB
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));
    adc_oneshot_chan_cfg_t ch_cfg = { .atten = ADC_ATTEN_DB_11, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, POT_CH, &ch_cfg));

    servo_init();

    printf("\nRAW   pot(deg)   servo(deg)   стан\n");
    printf("----------------------------------------\n");

    uint32_t cnt = 0;
    while (1) {
        uint32_t raw     = read_pot();
        uint32_t pot_deg = raw * POT_RANGE_DEG / ADC_MAX;        // фактичний кут ручки

        // 1:1 — кут серво = кут ручки, але не більше ходу серво (обрізання)
        uint32_t srv_deg = (pot_deg > SERVO_RANGE_DEG) ? SERVO_RANGE_DEG : pot_deg;
        servo_set_deg(srv_deg);

        if (++cnt >= LOG_EVERY) {
            cnt = 0;
            printf("%4lu   %6lu     %6lu       %s\n",
                   (unsigned long)raw, (unsigned long)pot_deg, (unsigned long)srv_deg,
                   (raw > RAW_LIMIT) ? "обрізано (поза діапазоном серво)" : "ok");
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
