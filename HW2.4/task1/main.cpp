#include <Arduino.h>

constexpr int BTN_PIN = 6;

volatile uint32_t presses = 0;

void IRAM_ATTR onFall() {
  presses++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onFall, FALLING);
  Serial.println("task1: no debounce, FALLING");
}

void loop() {
  static uint32_t lastPrinted = 0;
  uint32_t now = presses;
  if (now != lastPrinted) {
    lastPrinted = now;
    Serial.printf("count: %lu\n", (unsigned long)now);
  }
}