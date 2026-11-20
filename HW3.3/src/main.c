/*
 * Модуль 3.3 — Широтно-імпульсна модуляція (PWM)
 * Потенціометр (GPIO4) керує:
 *   - яскравістю світлодіода (GPIO15): LEDC timer0, канал 0, 5 кГц, 10 біт;
 *   - кутом сервоприводу SG90 (GPIO7): LEDC timer1, канал 1, 50 Гц, 14 біт.
 * Кожен вихід має власний таймер і канал, тому світлодіод і серво
 * не впливають на роботу одне одного.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

// ---------- Піни ----------
#define POT_CH        ADC_CHANNEL_3        // GPIO4 = ADC1_CH3
#define LED_GPIO      15
#define SERVO_GPIO    7

// ---------- ШІМ світлодіода ----------
#define LED_FREQ      5000                 // 5 кГц — без помітного мерехтіння
#define LED_RES       LEDC_TIMER_10_BIT    // duty 0..1023
#define LED_MAX       1023

// ---------- ШІМ сервоприводу ----------
#define SERVO_FREQ    50                   // 50 Гц = період 20 мс
#define SERVO_RES     LEDC_TIMER_14_BIT    // duty 0..16383
#define SERVO_MAX     16383
#define SERVO_MIN_US  500                  // імпульс для 0°
#define SERVO_MAX_US  2500                 // імпульс для 180°
#define SERVO_PERIOD_US 20000

// ---------- ADC ----------
#define ADC_MAX       4095                 // 12 біт
#define AVG_N         8                    // усереднення від шуму
#define PERIOD_MS     20                   // оновлення 50 разів на секунду

static adc_oneshot_unit_handle_t adc;

// Усереднене читання потенціометра (0..4095)
static int read_pot(void)
{
    int sum = 0;
    for (int i = 0; i < AVG_N; i++) {
        int r = 0;
        adc_oneshot_read(adc, POT_CH, &r);
        sum += r;
    }
    return sum / AVG_N;
}

// Налаштування таймера та каналу LEDC
static void pwm_init(ledc_timer_t timer, ledc_channel_t ch, int gpio,
                     uint32_t freq, ledc_timer_bit_t res)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = timer,
        .duty_resolution = res,
        .freq_hz         = freq,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = ch,
        .timer_sel  = timer,
        .gpio_num   = gpio,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

// Записати нове значення duty в канал
static void pwm_set(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

void app_main(void)
{
    // 1. Ініціалізація ADC1, канал GPIO4: 12 біт, атенюація 11 dB
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, POT_CH, &ch_cfg));

    // 2. Два незалежні ШІМ на різних таймерах
    pwm_init(LEDC_TIMER_0, LEDC_CHANNEL_0, LED_GPIO,   LED_FREQ,   LED_RES);
    pwm_init(LEDC_TIMER_1, LEDC_CHANNEL_1, SERVO_GPIO, SERVO_FREQ, SERVO_RES);

    printf("\nPOT    LED_duty(%%)   pulse(us)   angle(deg)\n");
    printf("-------------------------------------------\n");

    int cnt = 0;
    while (1) {
        // 3. Читаємо потенціометр
        int pot = read_pot();

        // 4. Світлодіод: 0..4095 → duty 0..1023
        uint32_t duty_led = pot * LED_MAX / ADC_MAX;

        // 5. Серво: 0..4095 → імпульс 500..2500 мкс → duty при періоді 20 мс
        uint32_t us = SERVO_MIN_US + pot * (SERVO_MAX_US - SERVO_MIN_US) / ADC_MAX;
        uint32_t duty_srv = us * SERVO_MAX / SERVO_PERIOD_US;
        float angle = (us - SERVO_MIN_US) * 180.0f / (SERVO_MAX_US - SERVO_MIN_US);

        // 6. Оновлюємо обидва виходи
        pwm_set(LEDC_CHANNEL_0, duty_led);
        pwm_set(LEDC_CHANNEL_1, duty_srv);

        // 7. Лог у серійну консоль раз на 200 мс
        if (++cnt >= 10) {
            cnt = 0;
            printf("%4d   %10.1f   %9lu   %10.1f\n",
                   pot, duty_led * 100.0f / LED_MAX,
                   (unsigned long)us, angle);
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
