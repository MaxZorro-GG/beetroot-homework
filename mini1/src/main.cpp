#include <Arduino.h>

const int PIN_LDR = 5;
const int PIN_RELAY = 7;
const int PIN_BOOT = 0;   // кнопка BOOT на платі

const int THRESHOLD_DARK = 1700;
const int THRESHOLD_LIGHT = 2400;

int mode = 0;  // 0 = датчик, 1 = завжди ON, 2 = все OFF
bool lastBoot = HIGH;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  digitalWrite(PIN_RELAY, LOW);

  Serial.println("mode 0: sensor");
}

void loop() {
  bool boot = digitalRead(PIN_BOOT);
  if (lastBoot == HIGH && boot == LOW) {  // натиснули
    delay(50);                            // debounce
    if (digitalRead(PIN_BOOT) == LOW) {
      mode++;
      if (mode > 2) mode = 0;

      if (mode == 0) Serial.println("mode 0: sensor");
      if (mode == 1) Serial.println("mode 1: always ON");
      if (mode == 2) Serial.println("mode 2: all OFF");

      while (digitalRead(PIN_BOOT) == LOW) delay(10);  // чекаємо відпускання
    }
  }
  lastBoot = boot;

  if (mode == 1) {
    digitalWrite(PIN_RELAY, HIGH);   // реле завжди увімкнене
  } else if (mode == 2) {
    digitalWrite(PIN_RELAY, LOW);    // усе вимкнено
  } else {
    int adc = analogRead(PIN_LDR);
    Serial.printf("ADC=%d mode=0\n", adc);

    if (adc < THRESHOLD_DARK) {
      digitalWrite(PIN_RELAY, HIGH);
    } else if (adc > THRESHOLD_LIGHT) {
      digitalWrite(PIN_RELAY, LOW);
    }
  }

  delay(20);
}