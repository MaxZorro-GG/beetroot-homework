#include <Arduino.h>

constexpr int pins[3] = {4, 5, 6};
constexpr uint32_t period[3] = {200, 500, 1000};
bool on[3] = {false, false, false};
uint32_t last[3] = {0, 0, 0};

void setup() {
  for (int i = 0; i < 3; i++) pinMode(pins[i], OUTPUT);
}

void loop() {
  uint32_t now = millis();
  for (int i = 0; i < 3; i++) {
    if (now - last[i] >= period[i]) {
      last[i] = now;
      on[i] = !on[i];
      digitalWrite(pins[i], on[i]);
    }
  }
}