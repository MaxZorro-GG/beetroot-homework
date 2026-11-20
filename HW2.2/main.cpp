#include <Arduino.h>

// Вимірювання часу спрацювання реле (від подачі сигналу до замикання контакту)
#define RELAY_PIN       4
#define CONTACT_PIN     5

#define N_MEAS          10        // кількість вимірювань
#define START_DELAY_MS  1000      // пауза перед стартом
#define TIMEOUT_US      100000    // максимум очікування замикання контакту, мкс
#define RELAY_OFF_MS    400       // пауза з вимкненим реле між вимірюваннями

uint32_t times[N_MEAS];           // результати, мкс
uint32_t count = 0;               // кількість виконаних вимірювань

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  delay(START_DELAY_MS);
  Serial.println("старт вимірювань");
}

void loop() {
  if (count >= N_MEAS) return;

  // Вмикаємо реле і чекаємо, поки контакт замкнеться (LOW)
  digitalWrite(RELAY_PIN, HIGH);
  uint32_t t0 = micros();
  while (digitalRead(CONTACT_PIN) == HIGH) {
    if (micros() - t0 > TIMEOUT_US) break;
  }
  uint32_t t1 = micros();

  digitalWrite(RELAY_PIN, LOW);
  delay(RELAY_OFF_MS);

  times[count] = t1 - t0;
  Serial.printf("%lu. %lu мкс\n", count + 1, times[count]);
  count++;

  // Після останнього вимірювання — середнє
  if (count == N_MEAS) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < N_MEAS; i++) sum += times[i];
    Serial.printf("середнє значення: %lu мкс\n", sum / N_MEAS);
  }
}
