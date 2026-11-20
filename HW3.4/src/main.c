/*
 * Модуль 3.4 — Синтезатор на зумері (PWM)
 * Пасивний зумер на GPIO5. Нота = частота ШІМ (LEDC), duty 50 %.
 * Клавіша 'm' у Serial Monitor перемикає режим:
 *   АВТО   — по черзі грають 4 мелодії;
 *   РУЧНИЙ — клавіші c d e f g a b C грають ноти.
 * Клавіші читаються напряму з UART0, накопичені натискання скидаються.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/uart.h"

#define BUZZ_GPIO  5
#define BUZZ_RES   LEDC_TIMER_10_BIT   // duty 0..1023
#define DUTY_ON    512                 // 50 % — найгучніше для зумера
#define CON_UART   UART_NUM_0          // порт Serial Monitor
#define KEY_MS     200                 // тривалість ноти в ручному режимі
#define SOUND_PCT  90                  // скільки % тривалості ноти звучить звук (решта — тиша)
#define MELODY_GAP_MS  1500            // пауза між мелодіями
#define GAP_STEP_MS    50              // крок паузи (щоб швидко реагувати на 'm')
#define IDLE_MS        50              // очікування в ручному режимі
#define UART_TIMEOUT_MS 20             // таймаут читання клавіші
#define UART_RX_BUF    256             // буфер прийому UART
#define KEYS_STACK     3072            // стек задачі клавіатури
#define KEYS_PRIO      5               // пріоритет задачі клавіатури

// Частоти нот 4-ї октави, Гц (0 = пауза)
#define C4 262
#define D4 294
#define E4 330
#define F4 349
#define G4 392
#define A4 440
#define B4 494
#define C5 523
#define REST 0

typedef struct { int freq; int ms; } note_t;                       // нота + тривалість
typedef struct { const char *name; const note_t *n; int len; } melody_t;

// ---------- Мелодії ----------
static const note_t baby_shark[] = {
    {C4,300},{D4,300},{E4,500},{REST,150},
    {C4,300},{D4,300},{E4,500},{REST,150},
    {C4,300},{D4,300},{E4,800}};
static const note_t jingle[] = {
    {E4,250},{E4,250},{E4,500},{E4,250},{E4,250},{E4,500},
    {E4,250},{G4,250},{C4,350},{D4,150},{E4,800}};
static const note_t rock_you[] = {
    {C4,300},{C4,300},{D4,500},{REST,200},
    {C4,300},{C4,300},{D4,500},{REST,200},
    {C4,300},{C4,300},{D4,300},{E4,800}};
static const note_t twinkle[] = {
    {C4,400},{C4,400},{G4,400},{G4,400},{A4,400},{A4,400},{G4,800}};

#define LEN(x) (sizeof(x) / sizeof(x[0]))
static const melody_t melodies[] = {
    {"Baby Shark",       baby_shark, LEN(baby_shark)},
    {"Jingle Bells",     jingle,     LEN(jingle)},
    {"We Will Rock You", rock_you,   LEN(rock_you)},
    {"Twinkle Twinkle",  twinkle,    LEN(twinkle)},
};

static volatile bool manual = false;   // поточний режим

// ---------- Зумер ----------
// Ініціалізація LEDC: таймер0, канал0, стартова частота 1 кГц, звук вимкнено
static void buzzer_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = BUZZ_RES, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .gpio_num = BUZZ_GPIO, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

// Увімкнути тон заданої частоти (0 = тиша)
static void tone(int freq)
{
    if (freq > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);   // висота ноти
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, DUTY_ON);
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Зіграти ноту: звук + коротка пауза, щоб однакові ноти не зливались
static void play_note(int freq, int ms)
{
    tone(freq);
    vTaskDelay(pdMS_TO_TICKS(ms * SOUND_PCT / 100));
    tone(0);
    vTaskDelay(pdMS_TO_TICKS(ms * (100 - SOUND_PCT) / 100));
}

// Клавіша -> частота (для ручного режиму)
static int key_to_freq(uint8_t ch)
{
    switch (ch) {
        case 'c': return C4; case 'd': return D4; case 'e': return E4;
        case 'f': return F4; case 'g': return G4; case 'a': return A4;
        case 'b': return B4; case 'C': return C5; default: return -1;
    }
}

// ---------- Задача клавіатури: слухає UART весь час ----------
static void keys_task(void *arg)
{
    uint8_t ch;
    while (1) {
        if (uart_read_bytes(CON_UART, &ch, 1, pdMS_TO_TICKS(UART_TIMEOUT_MS)) == 1) {
            if (ch == 'm') {                         // перемикання режиму
                manual = !manual;
                tone(0);
                uart_flush_input(CON_UART);
                printf("\n=== Режим: %s ===\n",
                       manual ? "РУЧНИЙ (c d e f g a b C)" : "АВТО (мелодії)");
            } else if (manual) {                     // гра нот з клавіатури
                int f = key_to_freq(ch);
                if (f > 0) {
                    printf("нота %c = %d Гц\n", ch, f);
                    play_note(f, KEY_MS);
                    uart_flush_input(CON_UART);      // скидаємо натискання, що накопичились під час ноти
                }
            }
        }
    }
}

void app_main(void)
{
    buzzer_init();

    // Драйвер UART0 з буфером прийому — читаємо клавіші без буферизації stdin
    ESP_ERROR_CHECK(uart_driver_install(CON_UART, UART_RX_BUF, 0, 0, NULL, 0));

    printf("\nСинтезатор. 'm' — перемкнути АВТО/РУЧНИЙ режим\n");
    xTaskCreate(keys_task, "keys", KEYS_STACK, NULL, KEYS_PRIO, NULL);

    int m = 0;
    while (1) {
        if (manual) {                                // у ручному режимі мелодії мовчать
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }
        const melody_t *mel = &melodies[m];
        printf("Грає: %s\n", mel->name);
        for (int i = 0; i < mel->len && !manual; i++) {   // 'm' перериває мелодію
            printf("  %4d Гц  %4d мс\n", mel->n[i].freq, mel->n[i].ms);
            play_note(mel->n[i].freq, mel->n[i].ms);
        }
        m = (m + 1) % LEN(melodies);
        for (int t = 0; t < MELODY_GAP_MS / GAP_STEP_MS && !manual; t++)   // пауза між мелодіями
            vTaskDelay(pdMS_TO_TICKS(GAP_STEP_MS));
    }
}
