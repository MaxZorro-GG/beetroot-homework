#include <Arduino.h>

#define BASE_PIN 4
#define BTN_PIN  6
#define PWM_CH   0
#define PWM_FREQ 1000
#define PWM_RES  8

const uint8_t levels[5] = {102, 153, 204, 230, 255};

void setup() {
  Serial.begin(115200);
  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(BASE_PIN, PWM_CH);
  pinMode(BTN_PIN, INPUT_PULLDOWN);
  ledcWrite(PWM_CH, levels[0]);
  Serial.println("40 / 60 / 80 / 90 / 100");
}

void loop() {
  static uint8_t idx = 0;
  static uint32_t lastPress = 0;
  static uint32_t modeStart = 0;

  if (digitalRead(BTN_PIN) == HIGH && millis() - lastPress > 500) {
    while (digitalRead(BTN_PIN) == HIGH) delay(10);
    uint32_t now = millis();
    if (modeStart != 0) {
      Serial.printf("тримав %lu мс\n", now - modeStart);
    }
    lastPress = now;
    modeStart = now;
    idx = (idx + 1) % 5;
    ledcWrite(PWM_CH, levels[idx]);
    Serial.printf("скважність: %d%%\n", levels[idx] * 100 / 255);
  }
}