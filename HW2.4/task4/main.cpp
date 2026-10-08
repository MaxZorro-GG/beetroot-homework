#include <Arduino.h>

constexpr int BTN_PIN = 6;
constexpr uint32_t POLL_MS = 10;

enum State { IDLE, PRESSED, HELD };

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.println("task4 fresh");
}

void loop() {
  static State state = IDLE;
  static uint32_t lastPoll = 0;
  static uint32_t accepted = 0;

  if (millis() - lastPoll < POLL_MS) return;
  lastPoll = millis();

  bool down = digitalRead(BTN_PIN) == LOW;

  switch (state) {
    case IDLE:
      if (down) state = PRESSED;
      break;
    case PRESSED:
      if (down) {
        accepted++;
        Serial.printf("task4: %lu\n", (unsigned long)accepted);
        state = HELD;
      } else {
        state = IDLE;
      }
      break;
    case HELD:
      if (!down) state = IDLE;
      break;
  }
}