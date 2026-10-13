#include <Arduino.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 1 = демо (12 с період, 3 с робота), 0 = реальний режим (1 година / 15 хв)
#define demo 1

struct Config {
  // Час роботи і період (змінюється через demo   )
  static uint32_t PERIOD_S()  { return demo ? 12 : 3600; }
  static uint32_t RUN_S()     { return demo ? 3  : 900; }
  static uint32_t MIN_OFF_S() { return demo ? 4  : 600; }  // захист від повторного пуску

  static uint64_t PERIOD_US() { return (uint64_t)PERIOD_S() * 1000000ULL; }
  static uint64_t RUN_US()    { return (uint64_t)RUN_S() * 1000000ULL; }

  // Піни
  static uint8_t RELAY_PIN()  { return 7; }   // реле (через транзистор)
  static uint8_t BUZZ_PIN()   { return 5; }   // піщалка
  static uint8_t BUTTON_PIN() { return 6; }   // кнопка ручного пуску
  static uint8_t I2C_SDA()    { return 8; }   // OLED SDA
  static uint8_t I2C_SCL()    { return 9; }   // OLED SCL

  static uint8_t RELAY_ON_LEVEL()       { return HIGH; }  // 1 = увімкнено
  static uint8_t BUTTON_PRESSED_LEVEL() { return LOW; }   // кнопка на GND

  static uint32_t WDT_TIMEOUT_S() { return 8; }  // таймаут watchdog
  static uint32_t POLL_MS()       { return 5; }  // опитування кнопки
};

Adafruit_SSD1306 oled(128, 32, &Wire, -1);

// Апаратні таймери
static hw_timer_t *periodTimer = nullptr;  // період (година)
static hw_timer_t *runTimer    = nullptr;  // тривалість роботи (15 хв)

static volatile bool     fanRunning = false;
static volatile uint32_t cycles     = 0;
static int64_t           lastStopUs = 0;
static uint32_t          rejected   = 0;

// Встановлення виходів (реле)
static void applyOutputs(bool running) {
  digitalWrite(Config::RELAY_PIN(),
               running ? Config::RELAY_ON_LEVEL() : !Config::RELAY_ON_LEVEL());
}

// Переривання періодичного таймера — запуск вентилятора
void IRAM_ATTR onPeriod() {
  if (fanRunning) return;          // уже працює — не перезапускаємо
  fanRunning = true;
  cycles++;
  applyOutputs(true);
  if (runTimer) {
    timerWrite(runTimer, 0);
    timerAlarmWrite(runTimer, Config::RUN_US(), false);
    timerAlarmEnable(runTimer);
  }
}

// Переривання таймера тривалості — зупинка
void IRAM_ATTR onRunEnd() {
  fanRunning = false;
  applyOutputs(false);
}

// Короткий звуковий сигнал при зміні стану
static void beep() {
  for (int i = 0; i < 400; i++) {
    digitalWrite(Config::BUZZ_PIN(), i & 1);
    delayMicroseconds(200);
  }
  digitalWrite(Config::BUZZ_PIN(), LOW);
}

// Опитування кнопки з антидребезгом
static bool buttonEdge() {
  static bool     was  = false;
  static uint32_t last = 0;
  if (millis() - last < Config::POLL_MS()) return false;
  last = millis();
  bool down = digitalRead(Config::BUTTON_PIN()) == Config::BUTTON_PRESSED_LEVEL();
  bool edge = down && !was;
  was = down;
  return edge;
}

// Ручний пуск з захистом
static void manualStart() {
  if (fanRunning) {
    rejected++;
    Serial.println("пуск відхилено: вже працює");
    return;
  }
  if (lastStopUs &&
      (esp_timer_get_time() - lastStopUs) < (int64_t)Config::MIN_OFF_S() * 1000000) {
    rejected++;
    Serial.println("пуск відхилено: рано після зупинки");
    return;
  }
  fanRunning = true;
  cycles++;
  applyOutputs(true);
  if (periodTimer) timerWrite(periodTimer, 0);   // скинути відлік періоду
  if (runTimer) {
    timerWrite(runTimer, 0);
    timerAlarmWrite(runTimer, Config::RUN_US(), false);
    timerAlarmEnable(runTimer);
  }
  Serial.println("ручний пуск");
}

// Виведення стану на OLED
static void draw() {
  uint32_t left = 0;
  if (fanRunning && runTimer) {
    uint64_t t = timerRead(runTimer);
    left = (t >= Config::RUN_US()) ? 0 : (Config::RUN_US() - t) / 1000000;
  } else if (periodTimer) {
    uint64_t t = timerRead(periodTimer);
    left = (t >= Config::PERIOD_US()) ? 0 : (Config::PERIOD_US() - t) / 1000000;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(2);
  const char *state = fanRunning ? "ON" : "OFF";
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(state, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((128 - w) / 2, 0);
  oled.println(state);

  oled.setTextSize(1);
  char buf[16];
  sprintf(buf, "left %lu s", (unsigned long)left);
  oled.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((128 - w) / 2, 20);
  oled.print(buf);

  oled.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("старт");

  // Налаштування пінів
  pinMode(Config::RELAY_PIN(), OUTPUT);
  pinMode(Config::BUZZ_PIN(), OUTPUT);
  pinMode(Config::BUTTON_PIN(), INPUT_PULLUP);
  applyOutputs(false);   // спочатку вимкнено

  // OLED
  Wire.begin(Config::I2C_SDA(), Config::I2C_SCL());
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED не знайдено");
  } else {
    Serial.println("OLED ok");
  }

  // Апаратні таймери (незалежно від loop)
  periodTimer = timerBegin(0, 80, true);
  runTimer    = timerBegin(1, 80, true);

  if (!periodTimer || !runTimer) {
    Serial.println("таймери не створилися");
  } else {
    timerAttachInterrupt(periodTimer, &onPeriod, true);
    timerAttachInterrupt(runTimer, &onRunEnd, true);
    timerAlarmWrite(periodTimer, Config::PERIOD_US(), true);
    timerAlarmEnable(periodTimer);
    Serial.println("таймери запущено");
  }

  // Watchdog — перезавантаження, якщо loop зависне
  esp_task_wdt_init(Config::WDT_TIMEOUT_S(), true);
  esp_task_wdt_add(NULL);

  Serial.printf("період %u с, робота %u с\n", Config::PERIOD_S(), Config::RUN_S());
}

void loop() {
  static bool lastRun = false;
  esp_task_wdt_reset();   // годуємо watchdog

  // Кнопка ручного пуску
  if (buttonEdge()) manualStart();

  // Зміна стану — звук і лог
  if (fanRunning != lastRun) {
    if (!fanRunning) lastStopUs = esp_timer_get_time();
    lastRun = fanRunning;
    beep();
    Serial.println(fanRunning ? "ON" : "OFF");
  }

  // Лог стану раз на секунду
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 1000) {
    lastLog = millis();
    Serial.printf("%s лишилось %lu с\n",
                  fanRunning ? "ON" : "OFF",
                  (unsigned long)(fanRunning
                    ? (Config::RUN_US() - (runTimer ? timerRead(runTimer) : 0)) / 1000000
                    : (Config::PERIOD_US() - (periodTimer ? timerRead(periodTimer) : 0)) / 1000000));
  }

  draw();
  delay(5);
}