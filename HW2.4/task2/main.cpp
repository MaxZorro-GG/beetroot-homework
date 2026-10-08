#include <Arduino.h>

constexpr int BTN_PIN = 6;
constexpr uint32_t DEBOUNCE_MS = 50;

volatile uint32_t edges = 0;
uint32_t accepted = 0;

void IRAM_ATTR onFall() {
  edges++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onFall, FALLING);
  Serial.println("task2: 50 ms debounce outside ISR");
}

void loop() {
  static uint32_t seen = 0;
  static uint32_t lastOk = 0;

  uint32_t nowEdges = edges;
  if (nowEdges == seen) return;
  seen = nowEdges;

  uint32_t now = millis();
  if (now - lastOk < DEBOUNCE_MS) {
    Serial.println("ignore");
    return;
  }

  lastOk = now;
  accepted++;
  Serial.printf("accepted: %lu\n", (unsigned long)accepted);
}