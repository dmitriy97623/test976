#include "hardware.h"
#include "logic.h" // Для доступа к глобальным переменным (setpointLow, calMin и т.д.)

// --- Конфигурация пинов ---
// Фиксированные номера пинов, используемых в проекте
const int PIN_SENSOR = A0;           // Аналоговый пин A0 для сигнала датчика
const int PIN_RELAY_1 = 2;           // Цифровой пин D2 для реле 1
const int PIN_RELAY_2 = 3;           // Цифровой пин D3 для реле 2
const int PIN_BTN_MENU = 4;          // Цифровой пин D4 для кнопки MENU
const int PIN_BTN_CHANGE = 5;        // Цифровой пин D5 для кнопки CHANGE

// --- Адреса в EEPROM ---
// Фиксированные адреса для хранения настроек в энергонезависимой памяти
const int ADDR_MAGIC = 0;             // Адрес магического числа
const int ADDR_RANGE = 1;             // Адрес индекса диапазона
const int ADDR_UNIT = 2;              // Адрес индекса единиц измерения
const int ADDR_SP_LOW_H = 3;          // Старший байт уставки Low
const int ADDR_SP_LOW_L = 4;          // Младший байт уставки Low
const int ADDR_SP_HIGH_H = 5;         // Старший байт уставки High
const int ADDR_SP_HIGH_L = 6;         // Младший байт уставки High
const int ADDR_HYST_H = 7;            // Старший байт гистерезиса
const int ADDR_HYST_L = 8;            // Младший байт гистерезиса
const int ADDR_CAL_MIN_H = 9;         // Старший байт калибровки 4 мА
const int ADDR_CAL_MIN_L = 10;        // Младший байт калибровки 4 мА
const int ADDR_CAL_MAX_H = 11;        // Старший байт калибровки 20 мА
const int ADDR_CAL_MAX_L = 12;        // Младший байт калибровки 20 мА

// Контрольное число для проверки инициализации EEPROM
const unsigned long MAGIC_NUM = 12345;

// --- Состояние аппаратных компонентов ---
// Статические переменные для отслеживания состояния реле
static bool valve1State = false;
static bool valve2State = false;

// Статический флаг для отслеживания ошибки датчика (обрыв)
static bool sensorErrorFlag = false;

// --- Функции инициализации ---
// Инициализация всех цифровых и аналоговых пинов
void hardwareSetup() {
  pinMode(PIN_RELAY_1, OUTPUT);       // Настраиваем пин реле 1 как выход
  pinMode(PIN_RELAY_2, OUTPUT);       // Настраиваем пин реле 2 как выход
  digitalWrite(PIN_RELAY_1, LOW);     // Инициализируем реле 1 в выключенном состоянии
  digitalWrite(PIN_RELAY_2, LOW);     // Инициализируем реле 2 в выключенном состоянии

  pinMode(PIN_BTN_MENU, INPUT_PULLUP);    // Настраиваем кнопку MENU с подтяжкой к VCC
  pinMode(PIN_BTN_CHANGE, INPUT_PULLUP);  // Настраиваем кнопку CHANGE с подтяжкой к VCC
}

// --- Функции работы с датчиком ---
// Чтение сырого значения с аналогового преобразователя (0-1023)
int readSensorRaw() {
  return analogRead(PIN_SENSOR);
}

// Получение текущего флага состояния датчика
bool isSensorError() {
  return sensorErrorFlag;
}

// --- Функции работы с реле ---
// Установка состояния указанного реле (1 или 2)
// Вход: valveNum - номер реле, state - желаемое состояние (true=ON, false=OFF)
void setValveState(int valveNum, bool state) {
  if (valveNum == 1) {
    valve1State = state;                    // Сохраняем состояние в переменную
    digitalWrite(PIN_RELAY_1, state ? HIGH : LOW); // Устанавливаем состояние пина
  } else if (valveNum == 2) {
    valve2State = state;
    digitalWrite(PIN_RELAY_2, state ? HIGH : LOW);
  }
}

// Получение текущего состояния указанного реле
// Вход: valveNum - номер реле
// Выход: текущее состояние реле (true=ON, false=OFF)
bool getValveState(int valveNum) {
  if (valveNum == 1) return valve1State;
  if (valveNum == 2) return valve2State;
  return false; // Если номер некорректен
}

// --- Функции работы с EEPROM ---
// Сохранение всех текущих настроек в энергонезависимую память
void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_NUM); // Записываем магическое число

  EEPROM.put(ADDR_RANGE, currentRangeIndex); // Сохраняем индекс диапазона
  EEPROM.put(ADDR_UNIT, currentUnitIndex);   // Сохраняем индекс единиц измерения

  // Сохраняем уставки и гистерезис, умноженные на 10 для хранения с одной десятичной цифрой
  int iLow = (int)(setpointLow * 10);
  int iHigh = (int)(setpointHigh * 10);
  int iHyst = (int)(hysteresis * 10);

  EEPROM.put(ADDR_SP_LOW_H, highByte(iLow));
  EEPROM.put(ADDR_SP_LOW_L, lowByte(iLow));
  EEPROM.put(ADDR_SP_HIGH_H, highByte(iHigh));
  EEPROM.put(ADDR_SP_HIGH_L, lowByte(iHigh));
  EEPROM.put(ADDR_HYST_H, highByte(iHyst));
  EEPROM.put(ADDR_HYST_L, lowByte(iHyst));

  // Сохраняем калибровочные значения АЦП
  EEPROM.put(ADDR_CAL_MIN_H, highByte(calMin));
  EEPROM.put(ADDR_CAL_MIN_L, lowByte(calMin));
  EEPROM.put(ADDR_CAL_MAX_H, highByte(calMax));
  EEPROM.put(ADDR_CAL_MAX_L, lowByte(calMax));
}

// Загрузка всех настроек из энергонезависимой памяти
void loadSettings() {
  unsigned long magic;
  EEPROM.get(ADDR_MAGIC, magic); // Считываем магическое число
  if (magic != MAGIC_NUM) {       // Если оно не совпадает, EEPROM не инициализирована
    resetSettings();              // Выполняем сброс настроек по умолчанию
    return;
  }

  // Загружаем индексы диапазона и единиц измерения
  EEPROM.get(ADDR_RANGE, currentRangeIndex);
  EEPROM.get(ADDR_UNIT, currentUnitIndex);

  byte h, l;

  // Загружаем и преобразуем нижнюю уставку
  EEPROM.get(ADDR_SP_LOW_H, h);
  EEPROM.get(ADDR_SP_LOW_L, l);
  setpointLow = (float)(word(h, l)) / 10.0;

  // Загружаем и преобразуем верхнюю уставку
  EEPROM.get(ADDR_SP_HIGH_H, h);
  EEPROM.get(ADDR_SP_HIGH_L, l);
  setpointHigh = (float)(word(h, l)) / 10.0;

  // Загружаем и преобразуем гистерезис
  EEPROM.get(ADDR_HYST_H, h);
  EEPROM.get(ADDR_HYST_L, l);
  hysteresis = (float)(word(h, l)) / 10.0;

  // Загружаем калибровочные значения
  EEPROM.get(ADDR_CAL_MIN_H, h);
  EEPROM.get(ADDR_CAL_MIN_L, l);
  calMin = word(h, l);

  EEPROM.get(ADDR_CAL_MAX_H, h);
  EEPROM.get(ADDR_CAL_MAX_L, l);
  calMax = word(h, l);

  // Защита: если калибровка некорректна (min >= max), сбрасываем к значениям по умолчанию
  if (calMin >= calMax) {
    calMin = 197;
    calMax = 983;
  }
}

// Сброс всех настроек к значениям по умолчанию
void resetSettings() {
  // Устанавливаем заводские значения
  currentRangeIndex = 2;
  currentUnitIndex = 1;
  setpointLow = 20.0;
  setpointHigh = 80.0;
  hysteresis = 2.0;
  calMin = 197;
  calMax = 983;

  // Сохраняем сброшенные значения в EEPROM
  saveSettings();
}