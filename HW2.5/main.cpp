#include <Arduino.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 1 = демо (12 с період, 3 с робота), 0 = реальний режим (1 година / 15 хв)
#define DEMO_MODE 1

// Час роботи і період, с
#define DEMO_PERIOD_S        12
#define DEMO_RUN_S           3
#define DEMO_MIN_OFF_S       4
#define REAL_PERIOD_S        3600    // 1 година
#define REAL_RUN_S           900     // 15 хвилин
#define REAL_MIN_OFF_S       600     // 10 хвилин

#define US_PER_S             1000000ULL

// Апаратні таймери: APB 80 МГц / 80 = 1 МГц, один тік = 1 мкс
#define PERIOD_TIMER_NUM     0
#define RUN_TIMER_NUM        1
#define TIMER_DIVIDER        80
#define TIMER_COUNT_UP       true

// OLED SSD1306 128x32 по I2C
#define OLED_WIDTH           128
#define OLED_HEIGHT          32
#define OLED_RESET_PIN       -1      // окремого піна скидання немає
#define OLED_I2C_ADDR        0x3C
#define OLED_STATE_TEXT_SIZE 2       // розмір шрифту для ON/OFF
#define OLED_INFO_TEXT_SIZE  1       // розмір шрифту для відліку
#define OLED_STATE_Y         0       // рядок стану, пікселі
#define OLED_INFO_Y          20      // рядок відліку, пікселі
#define OLED_TEXT_BUF_LEN    16

// Піщалка: 400 півперіодів по 200 мкс = 2,5 кГц протягом 80 мс
#define BEEP_HALF_PERIODS    400
#define BEEP_HALF_PERIOD_US  200

#define SERIAL_BAUD          115200
#define STARTUP_DELAY_MS     500     // пауза, щоб USB CDC встиг підключитися
#define LOG_INTERVAL_MS      1000    // лог стану в Serial
#define LOOP_DELAY_MS        5

struct Config {
  // Час роботи і період (перемикається через DEMO_MODE)
  static uint32_t PERIOD_S()  { return DEMO_MODE ? DEMO_PERIOD_S  : REAL_PERIOD_S; }
  static uint32_t RUN_S()     { return DEMO_MODE ? DEMO_RUN_S     : REAL_RUN_S; }
  static uint32_t MIN_OFF_S() { return DEMO_MODE ? DEMO_MIN_OFF_S : REAL_MIN_OFF_S; }  // захист від повторного пуску

  static uint64_t PERIOD_US() { return (uint64_t)PERIOD_S() * US_PER_S; }
  static uint64_t RUN_US()    { return (uint64_t)RUN_S() * US_PER_S; }

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

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

// Апаратні таймери
static hw_timer_t *periodTimer = nullptr;  // період (година)
static hw_timer_t *runTimer    = nullptr;  // тривалість роботи (15 хв)

// fanRunning пишуть і ISR таймерів, і loop (ручний пуск),
// тому перевірку+зміну робимо в критичній секції під цим м'ютексом
static portMUX_TYPE      fanMux     = portMUX_INITIALIZER_UNLOCKED;
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
  portENTER_CRITICAL_ISR(&fanMux);
  if (fanRunning) {                // уже працює — не перезапускаємо
    portEXIT_CRITICAL_ISR(&fanMux);
    return;
  }
  fanRunning = true;
  cycles++;
  portEXIT_CRITICAL_ISR(&fanMux);
  applyOutputs(true);
  if (runTimer) {
    timerWrite(runTimer, 0);
    timerAlarmWrite(runTimer, Config::RUN_US(), false);
    timerAlarmEnable(runTimer);
  }
}

// Переривання таймера тривалості — зупинка
void IRAM_ATTR onRunEnd() {
  portENTER_CRITICAL_ISR(&fanMux);
  fanRunning = false;
  portEXIT_CRITICAL_ISR(&fanMux);
  applyOutputs(false);
}

// Короткий звуковий сигнал при зміні стану
static void beep() {
  for (uint32_t i = 0; i < BEEP_HALF_PERIODS; i++) {
    digitalWrite(Config::BUZZ_PIN(), i & 1);
    delayMicroseconds(BEEP_HALF_PERIOD_US);
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
  const bool tooSoon = lastStopUs &&
      (esp_timer_get_time() - lastStopUs) < (int64_t)(Config::MIN_OFF_S() * US_PER_S);

  // Перевірка і встановлення атомарно: між ними не встигне спрацювати onPeriod
  portENTER_CRITICAL(&fanMux);
  const bool alreadyRunning = fanRunning;
  const bool start = !alreadyRunning && !tooSoon;
  if (start) {
    fanRunning = true;
    cycles++;
  }
  portEXIT_CRITICAL(&fanMux);

  if (alreadyRunning) {
    rejected++;
    Serial.println("пуск відхилено: вже працює");
    return;
  }
  if (tooSoon) {
    rejected++;
    Serial.println("пуск відхилено: рано після зупинки");
    return;
  }
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
static void draw(bool running) {
  uint32_t left = 0;
  if (running && runTimer) {
    uint64_t t = timerRead(runTimer);
    left = (t >= Config::RUN_US()) ? 0 : (Config::RUN_US() - t) / US_PER_S;
  } else if (periodTimer) {
    uint64_t t = timerRead(periodTimer);
    left = (t >= Config::PERIOD_US()) ? 0 : (Config::PERIOD_US() - t) / US_PER_S;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(OLED_STATE_TEXT_SIZE);
  const char *state = running ? "ON" : "OFF";
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(state, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((OLED_WIDTH - w) / 2, OLED_STATE_Y);
  oled.println(state);

  oled.setTextSize(OLED_INFO_TEXT_SIZE);
  char buf[OLED_TEXT_BUF_LEN];
  snprintf(buf, sizeof(buf), "left %lu s", (unsigned long)left);
  oled.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((OLED_WIDTH - w) / 2, OLED_INFO_Y);
  oled.print(buf);

  oled.display();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(STARTUP_DELAY_MS);
  Serial.println("старт");

  // Налаштування пінів
  pinMode(Config::RELAY_PIN(), OUTPUT);
  pinMode(Config::BUZZ_PIN(), OUTPUT);
  pinMode(Config::BUTTON_PIN(), INPUT_PULLUP);
  applyOutputs(false);   // спочатку вимкнено

  // OLED
  Wire.begin(Config::I2C_SDA(), Config::I2C_SCL());
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("OLED не знайдено");
  } else {
    Serial.println("OLED ok");
  }

  // Апаратні таймери (незалежно від loop)
  periodTimer = timerBegin(PERIOD_TIMER_NUM, TIMER_DIVIDER, TIMER_COUNT_UP);
  runTimer    = timerBegin(RUN_TIMER_NUM, TIMER_DIVIDER, TIMER_COUNT_UP);

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

  // Знімок стану: далі в ітерації працюємо з одним значенням
  portENTER_CRITICAL(&fanMux);
  const bool running = fanRunning;
  portEXIT_CRITICAL(&fanMux);

  // Зміна стану — звук і лог
  if (running != lastRun) {
    if (!running) lastStopUs = esp_timer_get_time();
    lastRun = running;
    beep();
    Serial.println(running ? "ON" : "OFF");
  }

  // Лог стану раз на секунду
  static uint32_t lastLog = 0;
  if (millis() - lastLog > LOG_INTERVAL_MS) {
    lastLog = millis();
    Serial.printf("%s лишилось %lu с\n",
                  running ? "ON" : "OFF",
                  (unsigned long)(running
                    ? (Config::RUN_US() - (runTimer ? timerRead(runTimer) : 0)) / US_PER_S
                    : (Config::PERIOD_US() - (periodTimer ? timerRead(periodTimer) : 0)) / US_PER_S));
  }

  draw(running);
  delay(LOOP_DELAY_MS);
}