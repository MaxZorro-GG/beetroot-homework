#include <Arduino.h>

// Вимірювання часу спрацювання реле (від подачі сигналу до замикання контакту)
#define RELAY_PIN       4
#define CONTACT_PIN     5

#define SERIAL_BAUD     115200
#define N_MEAS          10        // кількість вимірювань
#define START_DELAY_MS  1000      // пауза перед стартом
#define TIMEOUT_US      100000    // максимум очікування замикання контакту, мкс
#define RELAY_OFF_MS    400       // пауза з вимкненим реле між вимірюваннями

uint32_t times[N_MEAS];           // результати успішних вимірювань, мкс
uint32_t count = 0;               // кількість виконаних спроб
uint32_t validCount = 0;          // кількість спроб без таймауту

void setup() {
  Serial.begin(SERIAL_BAUD);
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
  bool timeout = false;
  while (digitalRead(CONTACT_PIN) == HIGH) {
    if (micros() - t0 > TIMEOUT_US) {
      timeout = true;             // контакт не замкнувся — це помилка, а не результат
      break;
    }
  }
  uint32_t t1 = micros();

  digitalWrite(RELAY_PIN, LOW);
  delay(RELAY_OFF_MS);

  count++;
  if (timeout) {
    // Таймаут не потрапляє в масив і не псує середнє
    Serial.printf("%lu. TIMEOUT (> %lu мкс)\n", count, (uint32_t)TIMEOUT_US);
  } else {
    times[validCount] = t1 - t0;
    Serial.printf("%lu. %lu мкс\n", count, times[validCount]);
    validCount++;
  }

  // Після останньої спроби — середнє лише по успішних вимірюваннях
  if (count == N_MEAS) {
    if (validCount == 0) {
      Serial.println("усі вимірювання завершились TIMEOUT");
      return;
    }
    uint32_t sum = 0;
    for (uint32_t i = 0; i < validCount; i++) sum += times[i];
    Serial.printf("середнє значення: %lu мкс (успішних %lu з %lu)\n",
                  sum / validCount, validCount, (uint32_t)N_MEAS);
  }
}
