#include <Arduino.h>        // базові функції Arduino
#include <ESP32Servo.h>     // бібліотека для керування сервоприводом

const int PIN_TRIG = 4;     // пін Trig ультразвукового датчика
const int PIN_ECHO = 5;     // пін Echo ультразвукового датчика
const int PIN_LED_RED = 2;  // червоний світлодіод — об'єкт виявлено
const int PIN_LED_GREEN = 1; // зелений світлодіод — все спокійно
const int PIN_SERVO = 6;    // пін керування сервоприводом

Servo radar;                // об'єкт сервопривода

int measureCm() {           // функція вимірювання відстані в сантиметрах
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000); // чекаємо відповідь Echo
  if (us == 0) return -1;   // немає відповіді — повертаємо помилку
  return (int)(us / 58);    // перетворюємо час у сантиметри
}

void setup() {              // налаштування при старті
  Serial.begin(115200);     // швидкість порту
  delay(1500);
  pinMode(PIN_TRIG, OUTPUT);   // Trig — вихід
  pinMode(PIN_ECHO, INPUT);    // Echo — вхід
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  radar.setPeriodHertz(50);    // частота ШІМ 50 Гц
  radar.attach(PIN_SERVO, 500, 2400); // підключити серву
  radar.write(90);             // стартова позиція — 90°
  Serial.println("radar start");
}

void loop() {                  // головний цикл
  static int a = 60;           // поточний кут сервопривода
  static int dir = 1;          // напрямок руху: 1 — вправо, -1 — вліво

  int cm = measureCm();        // виміряти відстань
  Serial.printf("%d,%d\n", a, cm); // надіслати кут і відстань у Processing

  if (cm > 0 && cm < 50) {     // об'єкт ближче 50 см
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(40);
    digitalWrite(PIN_LED_RED, LOW);
    delay(40);
    return;                    // серва стоїть, кут не змінюємо
  }

  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
  a += dir;                    // рухаємо серву
  if (a >= 120) dir = -1;      // досягли правого краю — повертаємо
  if (a <= 60) dir = 1;        // досягли лівого краю — повертаємо
  radar.write(a);
  delay(15);
}