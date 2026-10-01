#include <Arduino.h>

// Конфігурація прошивки: піни, інтервали, параметри вимірювання.
// Усі значення — constexpr, щоб компілятор підставив їх на етапі збірки.
struct BlinkConfig {
  static constexpr uint8_t LED_PIN = 16;       // Пін світлодіода
  static constexpr uint8_t BUTTON_PIN = 7;     // Пін кнопки (на GND)

  static constexpr bool LED_ACTIVE_HIGH = true; // true: HIGH = увімкнено

  static constexpr uint32_t BLINK_INTERVAL_MS = 500; // Час миготіння, мс
  static constexpr uint32_t SERIAL_BAUD = 115200;    // Швидкість UART

  static constexpr uint32_t DEBOUNCE_MS = 50;        // Антидребезг, мс

  // Скільки ітерацій superloop збираємо перед друком статистики
  static constexpr uint32_t MEASURE_WINDOW = 1000;

  // Повний період миготіння: увімкнено + вимкнено
  static constexpr uint32_t periodMs() {
    return BLINK_INTERVAL_MS * 2;
  }
};

// Збірка впаде, якщо інтервал нульовий або від'ємний
static_assert(
  BlinkConfig::BLINK_INTERVAL_MS > 0,
  "Blink interval must be positive"
);

// Стан світлодіода. Базовий тип uint8_t — щоб enum займав 1 байт.
enum class LedState : uint8_t {
  Off = 0,
  On = 1
};

// Обгортка над GPIO світлодіода.
// Знає полярність і не пише в пін, якщо стан не змінився.
class Led {
private:
  const uint8_t pin_;
  const bool activeHigh_;

  LedState state_ = LedState::Off;

  // Перетворює логічний стан на рівень піна з урахуванням полярності
  constexpr uint8_t levelFor(LedState state) const {
    return (state == LedState::On) == activeHigh_
             ? HIGH
             : LOW;
  }

public:
  constexpr explicit Led(
    uint8_t pin,
    bool activeHigh = true
  )
    : pin_(pin),
      activeHigh_(activeHigh) {}

  // Налаштувати пін як вихід і погасити світлодіод
  void init() {
    pinMode(pin_, OUTPUT);

    state_ = LedState::Off;

    digitalWrite(
      pin_,
      levelFor(state_)
    );
  }

  // Встановити стан. Повторний запис того самого стану ігнорується.
  void set(LedState state) {
    if (state == state_) {
      return;
    }

    state_ = state;

    digitalWrite(
      pin_,
      levelFor(state_)
    );
  }

  // Інвертувати поточний стан
  void toggle() {
    set(
      state_ == LedState::On
        ? LedState::Off
        : LedState::On
    );
  }

  LedState state() const {
    return state_;
  }
};

// Режими роботи, які перемикає кнопка по колу:
// Blink -> AlwaysOn -> AlwaysOff -> Blink -> AlwaysOn -> AlwaysOff -> Blink
enum class LedMode : uint8_t {
  Blink,
  AlwaysOn,
  AlwaysOff
};

// Єдиний екземпляр світлодіода
static Led led(
  BlinkConfig::LED_PIN,
  BlinkConfig::LED_ACTIVE_HIGH
);

static LedMode ledMode = LedMode::Blink;

// Момент останнього перемикання в режимі Blink (millis)
static uint32_t lastToggleMs = 0;

// Прапорець із ISR. volatile обов'язковий: його пише переривання,
// а читає основний цикл. true на старті — одразу обробити кнопку
// і перейти в ALWAYS_ON.
volatile bool buttonPressed = false;

// Статистика тіла циклу за вікно вимірювання
static uint32_t windowIterations = 0;
static uint32_t windowTotalUs = 0;
static uint32_t windowMinUs = UINT32_MAX;
static uint32_t windowMaxUs = 0;

// Статистика періоду між початками ітерацій (час усього superloop)
static uint32_t previousLoopStartUs = 0;
static uint32_t periodTotalUs = 0;
static uint32_t periodMaxUs = 0;

// Момент останньої прийнятої обробки кнопки (для антидребезгу)
static uint32_t lastButtonHandledMs = 0;

// Обробник переривання. Має бути мінімальним:
// без Serial, millis, delay і будь-якої іншої логіки.
// IRAM_ATTR — щоб функція лежала в IRAM і не чекала flash.
void IRAM_ATTR onButtonPress() {
  buttonPressed = true;
}

void setup() {

  Serial.begin(
    BlinkConfig::SERIAL_BAUD
  );

  led.init();

  pinMode(
    BlinkConfig::BUTTON_PIN,
    INPUT_PULLUP
  );

  attachInterrupt(
    digitalPinToInterrupt(
      BlinkConfig::BUTTON_PIN
    ),
    onButtonPress,
    FALLING
  );

  Serial.println();

  Serial.printf(
    "LED GPIO: %u\r\n",
    BlinkConfig::LED_PIN
  );

  Serial.printf(
    "BUTTON GPIO: %u\r\n",
    BlinkConfig::BUTTON_PIN
  );

  Serial.printf(
    "Blink interval: %u ms\r\n",
    BlinkConfig::BLINK_INTERVAL_MS
  );

  Serial.printf(
    "Blink period: %u ms\r\n",
    BlinkConfig::periodMs()
  );

  Serial.printf(
    "sizeof(Led): %u B\r\n",
    sizeof(Led)
  );

  Serial.printf(
    "sizeof(LedState): %u B\r\n",
    sizeof(LedState)
  );

  Serial.println();

  Serial.println(
    "MODE -> BLINK"
  );

  Serial.println();

  // Опорна точка для вимірювання періоду першої ітерації
  previousLoopStartUs = micros();
}
============================================================
//        LOOP
// ============================================================
void loop() {

  const uint32_t loopStartUs = micros();

  const uint32_t periodUs =
    loopStartUs - previousLoopStartUs;

  previousLoopStartUs = loopStartUs;

  // Перша ітерація вікна не має попереднього періоду — її пропускаємо
  if (windowIterations > 0) {

    periodTotalUs += periodUs;

    if (periodUs > periodMaxUs) {
      periodMaxUs = periodUs;
    }
  }

  const uint32_t nowMs = millis();

  // Кнопку обробляємо в основному циклі, не в ISR
  if (buttonPressed) {

    buttonPressed = false;

    // Антидребезг: ігноруємо повторні спрацювання в межах DEBOUNCE_MS
    if (
      nowMs - lastButtonHandledMs
      >= BlinkConfig::DEBOUNCE_MS
    ) {

      lastButtonHandledMs = nowMs;

      // BLINK -> ALWAYS_ON
      if (ledMode == LedMode::Blink) {

        ledMode = LedMode::AlwaysOn;

        led.set(
          LedState::On
        );

        Serial.println(
          "MODE -> ALWAYS_ON"
        );
      }

      // ALWAYS_ON -> ALWAYS_OFF
      else if (ledMode == LedMode::AlwaysOn) {

        ledMode = LedMode::AlwaysOff;

        led.set(
          LedState::Off
        );

        Serial.println(
          "MODE -> ALWAYS_OFF"
        );
      }

      // ------------------------------------------------------
      // ALWAYS_OFF -> BLINK
      // ------------------------------------------------------

      else {

        ledMode = LedMode::Blink;

        // Світлодіод лишаємо вимкненим.
        // Наступне перемикання буде через BLINK_INTERVAL_MS.
        Serial.println(
          "MODE -> BLINK"
        );
      }
    }
  }

  // ----------------------------------------------------------
  // Керування світлодіодом за поточним режимом
  // ----------------------------------------------------------

  switch (ledMode) {

    case LedMode::Blink:

      if (
        nowMs - lastToggleMs
        >= BlinkConfig::BLINK_INTERVAL_MS
      ) {

        lastToggleMs = nowMs;

        led.toggle();
      }

      break;

    case LedMode::AlwaysOn:

      led.set(
        LedState::On
      );

      break;

    case LedMode::AlwaysOff:

      led.set(
        LedState::Off
      );

      break;
  }

  // ----------------------------------------------------------
  // Кінець ітерації: час виконання тіла циклу
  // ----------------------------------------------------------

  const uint32_t loopEndUs = micros();

  const uint32_t bodyUs =
    loopEndUs - loopStartUs;

  
  windowIterations++;
  windowTotalUs += bodyUs;

  if (bodyUs < windowMinUs) {
    windowMinUs = bodyUs;
  }

  if (bodyUs > windowMaxUs) {
    windowMaxUs = bodyUs;
  }

  if (
    windowIterations
    >= BlinkConfig::MEASURE_WINDOW
  ) {

    const uint32_t averageBodyUs =
      windowTotalUs / windowIterations;

    const uint32_t averagePeriodUs =
      periodTotalUs / windowIterations;

    // body_* — час роботи самого loop
    // period_* — інтервал між початками ітерацій (з урахуванням усього циклу)
    Serial.printf(
      "window=%lu | "
      "body_avg=%lu us | "
      "body_min=%lu us | "
      "body_max=%lu us | "
      "period_avg=%lu us | "
      "period_max=%lu us | "
      "mode=",
      windowIterations,
      averageBodyUs,
      windowMinUs,
      windowMaxUs,
      averagePeriodUs,
      periodMaxUs
    );

    switch (ledMode) {

      case LedMode::Blink:
        Serial.println("BLINK");
        break;

      case LedMode::AlwaysOn:
        Serial.println("ALWAYS_ON");
        break;

      case LedMode::AlwaysOff:
        Serial.println("ALWAYS_OFF");
        break;
    }

    // --------------------------------------------------------
    // Скидання вікна вимірювання
    // --------------------------------------------------------

    windowIterations = 0;

    windowTotalUs = 0;

    windowMinUs = UINT32_MAX;

    windowMaxUs = 0;

    periodTotalUs = 0;

    periodMaxUs = 0;
  }
}