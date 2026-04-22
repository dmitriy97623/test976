/*
 * Скетч для Arduino Nano
 * Чтение сигнала 4–20 мА через шунт 250 Ом
 * Автор: GigaCode (для Дмитрия)
 * Версия: 1.0
 */

// Пины
const int ANALOG_PIN = A0;    // Подключено к шунту 250 Ом
const int RELAY_PIN = 7;      // Управление клапаном (реле)

// Калибровка
const float VREF = 5.0;       // Напряжение питания Arduino
const float SHUNT_RESISTOR = 250.0;  // Шунт 250 Ом → 1 В при 4 мА, 5 В при 20 мА

// Диапазон давления
const float PRESSURE_MIN = 0.0;     // бар
const float PRESSURE_MAX = 10.0;    // бар

// Пороги для обратной связи по клапану
const float CLOSE_THRESHOLD = 0.1;  // < 0.1 бар → закрыт
const float OPEN_THRESHOLD = 0.9;   // > 0.9 бар → открыт
const float HYSTERESIS = 0.05;      // гистерезис

// Переменные
float voltage = 0.0;
float current = 0.0;
float pressure = 0.0;
bool valve_open = false;           // состояние клапана (обратная связь)
bool last_valve_state = false;

void setup() {
  pinMode(ANALOG_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // начальное состояние — клапан закрыт

  Serial.begin(9600);
  while (!Serial);  // ждём подключения (для совместимости)

  Serial.println("Старт: датчик 4-20 мА");
}

void loop() {
  // Чтение АЦП (0–1023)
  int adc_value = analogRead(ANALOG_PIN);

  // Перевод в напряжение (V)
  voltage = (adc_value / 1023.0) * VREF;

  // Перевод в ток (мА): 1 В = 4 мА, 5 В = 20 мА
  current = voltage * 4.0;  // коэффициент: 4 мА/В

  // Перевод в давление (бар)
  if (current < 4.0) {
    pressure = 0.0;
  } else {
    pressure = ((current - 4.0) / 16.0) * (PRESSURE_MAX - PRESSURE_MIN) + PRESSURE_MIN;
  }

  // Гистерезис для определения состояния клапана
  if (valve_open) {
    if (pressure < (OPEN_THRESHOLD - HYSTERESIS)) {
      valve_open = false;
    }
  } else {
    if (pressure > (CLOSE_THRESHOLD + HYSTERESIS)) {
      valve_open = true;
    }
  }

  // Управление реле (для примера — можно использовать как выход)
  digitalWrite(RELAY_PIN, valve_open ? HIGH : LOW);

  // Отправка данных в формате: ADC,Current(mA),Pressure(bar),ValveState
  Serial.print(adc_value);
  Serial.print(",");
  Serial.print(current, 2);
  Serial.print(",");
  Serial.print(pressure, 2);
  Serial.print(",");
  Serial.println(valve_open ? "OPEN" : "CLOSED");

  delay(500);  // интервал обновления
}