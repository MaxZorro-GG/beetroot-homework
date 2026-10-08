#include <Arduino.h>

constexpr int BTN_PIN = 6;
constexpr uint32_t DEBOUNCE_MS = 50;

volatile bool event = false;
uint32_t accepted = 0;

void IRAM_ATTR onFall() {
  event = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onFall, FALLING);
  Serial.println("task3: accept only if still pressed");
}

void loop() {
  static uint32_t lastOk = 0;
  if (!event) return;
  event = false;

  uint32_t now = millis();
  if (now - lastOk < DEBOUNCE_MS) return;
  if (digitalRead(BTN_PIN) != LOW) {
    Serial.println("release ignored");
    return;
  }

  lastOk = now;
  accepted++;
  Serial.printf("accepted: %lu\n", (unsigned long)accepted);
}