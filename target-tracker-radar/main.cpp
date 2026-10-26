#include <Arduino.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <ESP32Servo.h>

// Піни датчиків, серви та енкодера
#define PIN_SDA1      8
#define PIN_SCL1      9
#define PIN_SDA2      11
#define PIN_SCL2      12
#define PIN_SERVO     6
#define PIN_ENC_CLK   4
#define PIN_ENC_DT    5
#define PIN_ENC_SW    7

VL53L1X sensor1;
VL53L1X sensor2;
Servo radar;

// Інтервали роботи (мс)
constexpr uint32_t SERVO_STEP_MS = 15;      // Крок серви при скануванні
constexpr uint32_t SENSOR_READ_MS = 40;     // Період зчитування датчиків

bool manualMode = false;                    // false = авто, true = ручний
int angle = 90;                             // Поточний кут серви
int dir = 1;                                // Напрямок сканування (+1 / -1)
int lastDist1 = -1;                         // Останнє значення датчика 1
int lastDist2 = -1;                         // Останнє значення датчика 2

uint32_t lastServoStep = 0;
uint32_t lastSensorRead = 0;

volatile int encPos = 0;                    // Позиція енкодера (змінюється в перериванні)
int lastEncPos = 0;
bool lastBtn = HIGH;

// Переривання енкодера (ручний режим)
void IRAM_ATTR encISR() {
  bool clk = digitalRead(PIN_ENC_CLK);
  bool dt  = digitalRead(PIN_ENC_DT);
  encPos += (clk == dt) ? 1 : -1;           // Визначаємо напрямок обертання
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Start");

  // Налаштування енкодера
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encISR, CHANGE);

  // Дві окремі I2C шини для двох датчиків
  Wire.begin(PIN_SDA1, PIN_SCL1);
  Wire.setClock(100000);

  Wire1.begin(PIN_SDA2, PIN_SCL2);
  Wire1.setClock(100000);

  // Ініціалізація серви
  radar.setPeriodHertz(50);
  radar.attach(PIN_SERVO, 500, 2400);
  radar.write(90);                          // Старт по центру

  // Ініціалізація першого датчика
  sensor1.setTimeout(500);
  if (!sensor1.init()) {
    Serial.println("Sensor1 init failed");
  } else {
    Serial.println("Sensor1 OK");
    sensor1.setDistanceMode(VL53L1X::Long);
    sensor1.setMeasurementTimingBudget(50000);
    sensor1.startContinuous(50);
  }

  // Ініціалізація другого датчика (на другій шині)
  sensor2.setTimeout(500);
  sensor2.setBus(&Wire1);
  if (!sensor2.init()) {
    Serial.println("Sensor2 init failed");
  } else {
    Serial.println("Sensor2 OK");
    sensor2.setDistanceMode(VL53L1X::Long);
    sensor2.setMeasurementTimingBudget(50000);
    sensor2.startContinuous(50);
  }

  lastServoStep = millis();
  lastSensorRead = millis();
}

void loop() {
  const uint32_t now = millis();

  // Перемикання режимів кнопкою енкодера
  bool btn = digitalRead(PIN_ENC_SW);
  if (btn == LOW && lastBtn == HIGH) {
    manualMode = !manualMode;
    Serial.println(manualMode ? "MANUAL" : "AUTO");
    delay(150);                             // Простий антидребезг
  }
  lastBtn = btn;

  // Періодичне зчитування датчиків
  if (now - lastSensorRead >= SENSOR_READ_MS) {
    lastSensorRead = now;

    int d1 = sensor1.read();
    if (!sensor1.timeoutOccurred()) lastDist1 = d1;

    int d2 = sensor2.read();
    if (!sensor2.timeoutOccurred()) lastDist2 = d2;

    Serial.printf("S1:%d  S2:%d  angle:%d  mode:%s\n",
                  lastDist1, lastDist2, angle,
                  manualMode ? "MAN" : "AUTO");
  }

  // ========== РУЧНИЙ РЕЖИМ ==========
  if (manualMode) {
    if (encPos != lastEncPos) {
      int delta = encPos - lastEncPos;
      angle += delta * 6;                  // Чутливість енкодера
      if (angle < 0) angle = 0;
      if (angle > 180) angle = 180;
      radar.write(angle);
      lastEncPos = encPos;
    }
  } 
  // ========== АВТОМАТИЧНИЙ РЕЖИМ ==========
  else {
    bool s1in = (lastDist1 > 30 && lastDist1 < 800);
    bool s2in = (lastDist2 > 30 && lastDist2 < 800);

    // Обидва датчики бачать ціль — центруємо
    if (s1in && s2in) {
      int minD = min(lastDist1, lastDist2);
      int maxD = max(lastDist1, lastDist2);
      int diffPercent = (maxD - minD) * 100 / maxD;

      if (diffPercent > 15) {
        if (now - lastServoStep >= 12) {
          lastServoStep = now;
          // Повертаємо серву в бік ближчого датчика
          if (lastDist1 < lastDist2) angle += 2;
          else                       angle -= 2;

          if (angle < 20) angle = 20;
          if (angle > 160) angle = 160;
          radar.write(angle);
        }
      }
    }
    // Бачить тільки один датчик — рухаємось у його бік
    else if (s1in || s2in) {
      if (now - lastServoStep >= 12) {
        lastServoStep = now;
        if (s1in) angle += 10;
        else      angle -= 10;

        if (angle < 20) angle = 20;
        if (angle > 160) angle = 160;
        radar.write(angle);
      }
    }
    // Нічого не бачимо — скануємо
    else {
      if (now - lastServoStep >= SERVO_STEP_MS) {
        lastServoStep = now;
        angle += dir * 2;
        if (angle >= 130) dir = -1;
        if (angle <= 50)  dir = 1;
        radar.write(angle);
      }
    }
  }
}
