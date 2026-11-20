#include <Arduino.h>

// Кнопка перемикає скважність ШІМ на базі транзистора: 40 / 60 / 80 / 90 / 100 %
#define BASE_PIN     4
#define BTN_PIN      6
#define PWM_CH       0
#define PWM_FREQ     1000
#define PWM_RES      8
#define PWM_MAX      255           // максимум duty для 8 біт
#define LEVELS_N     5             // кількість режимів
#define DEBOUNCE_MS  500           // мінімальний інтервал між натисканнями
#define BTN_POLL_MS  10            // опитування кнопки, поки вона затиснута

const uint8_t levels[LEVELS_N] = {102, 153, 204, 230, 255};   // 40/60/80/90/100 %

void setup() {
  Serial.begin(115200);
  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(BASE_PIN, PWM_CH);
  pinMode(BTN_PIN, INPUT_PULLDOWN);
  ledcWrite(PWM_CH, levels[0]);
  Serial.println("40 / 60 / 80 / 90 / 100");
}

void loop() {
  static uint32_t idx = 0;
  static uint32_t lastPress = 0;
  static uint32_t modeStart = 0;

  if (digitalRead(BTN_PIN) == HIGH && millis() - lastPress > DEBOUNCE_MS) {
    while (digitalRead(BTN_PIN) == HIGH) delay(BTN_POLL_MS);   // чекаємо відпускання
    uint32_t now = millis();
    if (modeStart != 0) {
      Serial.printf("тримав %lu мс\n", now - modeStart);
    }
    lastPress = now;
    modeStart = now;
    idx = (idx + 1) % LEVELS_N;
    ledcWrite(PWM_CH, levels[idx]);
    Serial.printf("скважність: %lu%%\n", (uint32_t)levels[idx] * 100 / PWM_MAX);
  }
}
