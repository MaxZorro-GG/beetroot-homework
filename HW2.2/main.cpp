#include <Arduino.h>

#define RELAY_PIN   4
#define CONTACT_PIN 5

const int N = 10;
uint32_t times[N];
int count = 0;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);
  Serial.println("старт измерений");
}

void loop() {
  if (count >= N) return;

  digitalWrite(RELAY_PIN, HIGH);
  uint32_t t0 = micros();

  while (digitalRead(CONTACT_PIN) == HIGH) {
    if (micros() - t0 > 100000) break;
  }
  uint32_t t1 = micros();

  digitalWrite(RELAY_PIN, LOW);
  delay(400);

  times[count] = t1 - t0;
  Serial.print(count + 1);
  Serial.print(". ");
  Serial.print(times[count]);
  Serial.println(" мкс");
  count++;

  if (count == N) {
    uint32_t sum = 0;
    for (int i = 0; i < N; i++) sum += times[i];
    Serial.print("середнє значення: ");
    Serial.print(sum / N);
    Serial.println(" мкс");
  }
}