#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define LDR_CH      ADC_CHANNEL_3   // GPIO4 на ESP32-S3 (ADC1_CH3)
#define LED_GPIO    GPIO_NUM_5      // світлодіод
#define SMA_N       16              // розмір вікна ковзної середньої
#define TH_ON       2200            // темніше -> LED увімк
#define TH_OFF      2800            // світліше -> LED вимк (гістерезис 600)

static int buf[SMA_N];              // кільцевий буфер відліків
static int idx = 0, count = 0;
static long sum = 0;                // сума значень у вікні

// Додає новий відлік і повертає середнє за останні SMA_N значень
static int sma_update(int x)
{
    sum -= buf[idx];                // прибрати найстаріший відлік
    buf[idx] = x;
    sum += x;                       // додати новий
    idx = (idx + 1) % SMA_N;        // зсунути індекс по колу
    if (count < SMA_N) count++;     // поки буфер не заповнений
    return sum / count;
}

void app_main(void)
{
    // Налаштування світлодіода
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Ініціалізація ADC1 у режимі oneshot
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &adc));

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,       // діапазон ~0..3.1 В
        .bitwidth = ADC_BITWIDTH_12,    // 0..4095
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, LDR_CH, &ccfg));

    bool led_on = false;

    while (1) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, LDR_CH, &raw));  // сире значення
        int avg = sma_update(raw);                             // відфільтроване

        // Гістерезис: два пороги, між ними стан не змінюється
        if (!led_on && avg < TH_ON) {
            led_on = true;
            gpio_set_level(LED_GPIO, 1);
        } else if (led_on && avg > TH_OFF) {
            led_on = false;
            gpio_set_level(LED_GPIO, 0);
        }

        printf("raw=%4d sma=%4d led=%d\n", raw, avg, led_on);
        vTaskDelay(pdMS_TO_TICKS(50));   // опитування 20 Гц
    }
}