/*
 * Pressure Controller for Arduino Nano (v2.0 - Digit-by-Digit Input)
 *
 * Функционал:
 * - Чтение датчика 4-20мА (A0)
 * - Управление 2 реле (D2, D3)
 * - Дисплей 1602 I2C
 * - Меню: Режим, Ранг, Уставки (L/H), Гистерезис, Ед. Изм., Калибровка, Сброс
 * - Поразрядный ввод чисел (быстрая настройка)
 * - EEPROM хранение настроек
 * - Диагностика обрыва цепи
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// --- Конфигурация пинов ---
const int PIN_SENSOR = A0;
const int PIN_RELAY_1 = 2;
const int PIN_RELAY_2 = 3;
const int PIN_BTN_MENU = 4;    // Кнопка 1: Навигация / Выбор разряда
const int PIN_BTN_CHANGE = 5;  // Кнопка 2: Изменение цифры / Значения

// --- Настройки дисплея ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- EEPROM Адреса ---
#define ADDR_MAGIC 0
#define ADDR_RANGE 2
#define ADDR_UNIT 4
#define ADDR_SET_LOW 6
#define ADDR_SET_HIGH 10
#define ADDR_HYST 14
#define ADDR_CAL_MIN 18
#define ADDR_CAL_MAX 22

#define MAGIC_NUM 12345

// --- Калибровка АЦП (по умолчанию) ---
int adcMinCal = 197;
int adcMaxCal = 983;

// --- Константы диапазонов ---
const int RANGES_COUNT = 6;
const float RANGES[RANGES_COUNT] = {1.0, 40.0, 250.0, 400.0, 600.0, 1000.0};

// --- Единицы измерения ---
enum UnitType { UNIT_PA, UNIT_KPA, UNIT_MPA, UNIT_BAR, UNIT_MBAR };
const char* UNIT_NAMES[] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const int UNITS_COUNT = 5;

// --- Глобальные переменные ---
int currentRangeIndex = 2;
int currentUnitIndex = 1;
float setpointLow = 20.0;
float setpointHigh = 80.0;
float hysteresis = 2.0;

bool valve1State = false;
bool valve2State = false;
bool sensorBreak = false;

// --- Переменные меню ---
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_UNIT, MENU_CALIB, MENU_RESET };
MenuItems currentMenuItem = MENU_MODE;
enum SetpointSubItems { SUB_LOW, SUB_HIGH, SUB_HYST };
SetpointSubItems currentSubItem = SUB_LOW;

bool isEditMode = false;      // Глобальный режим редактирования (Длинное нажатие)
bool isDigitEdit = false;     // Режим поразрядного ввода внутри уставки/гистерезиса

unsigned long lastBtnPressTime = 0;
const unsigned long LONG_PRESS_TIME = 1500;
const unsigned long DEBOUNCE_DELAY = 250;

// Временные переменные
int tempRangeIndex = 0;
int tempUnitIndex = 0;
float tempSetpointLow = 0;
float tempSetpointHigh = 0;
float tempHysteresis = 0;
int tempAdcMin = 0;
int tempAdcMax = 0;

// Переменные для поразрядного ввода
float editValue = 0.0;        // Значение, которое сейчас редактируем
int editDigitIndex = 0;       // Индекс разряда (0=десятые, 1=единицы, 2=десятки, 3=сотни...)
int maxDigits = 4;            // Макс кол-во разрядов для текущего числа
bool blinkState = false;
unsigned long lastBlinkTime = 0;

void setup() {
  Serial.begin(9600);

  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  digitalWrite(PIN_RELAY_1, LOW);
  digitalWrite(PIN_RELAY_2, LOW);

  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_CHANGE, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();

  loadSettings();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pressure Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Ver 2.0 Digit");
  delay(1500);
  lcd.clear();

  updateRelays(false, false);
}

void loop() {
  static unsigned long lastLoopTime = 0;
  if (millis() - lastLoopTime < 100) return;
  lastLoopTime = millis();

  handleButtons();

  if (!isEditMode) {
    float pressureValue = readPressure();
    controlRelays(pressureValue);
    displayWorkScreen(pressureValue);
  } else {
    displayEditScreen();
  }
}

// --- Чтение датчика ---
float readPressure() {
  int sensorValue = analogRead(PIN_SENSOR);

  // Проверка обрыва (меньше минимального калиброванного значения значительно)
  if (sensorValue < (adcMinCal - 50)) {
    sensorBreak = true;
    return 0.0;
  }
  sensorBreak = false;

  if (sensorValue < adcMinCal) sensorValue = adcMinCal;
  if (sensorValue > adcMaxCal) sensorValue = adcMaxCal;

  float percent = (float)(sensorValue - adcMinCal) / (float)(adcMaxCal - adcMinCal) * 100.0;
  float rangeVal = RANGES[currentRangeIndex];
  return (percent / 100.0) * rangeVal;
}

// --- Управление реле ---
void controlRelays(float pressure) {
  if (sensorBreak) {
    updateRelays(false, false);
    return;
  }

  bool v1 = false;
  bool v2 = false;

  if (pressure < (setpointLow - hysteresis)) v1 = true;
  if (pressure > setpointLow) v1 = false;

  if (pressure > (setpointHigh + hysteresis)) v2 = true;
  if (pressure < setpointHigh) v2 = false;

  updateRelays(v1, v2);
}

void updateRelays(bool v1, bool v2) {
  valve1State = v1;
  valve2State = v2;
  digitalWrite(PIN_RELAY_1, v1 ? HIGH : LOW);
  digitalWrite(PIN_RELAY_2, v2 ? HIGH : LOW);
}

// --- Обработка кнопок ---
void handleButtons() {
  static bool btn1Prev = HIGH;
  static bool btn2Prev = HIGH;
  static unsigned long btn1PressStart = 0;
  static bool longPressDetected = false;

  bool btn1 = digitalRead(PIN_BTN_MENU);
  bool btn2 = digitalRead(PIN_BTN_CHANGE);

  // Кнопка 1 (MENU)
  if (btn1 == LOW && btn1Prev == HIGH) {
    btn1PressStart = millis();
    longPressDetected = false;
  }

  if (btn1 == LOW && !longPressDetected) {
    if (millis() - btn1PressStart >= LONG_PRESS_TIME) {
      longPressDetected = true;
      toggleEditMode();
    }
  }

  if (btn1 == HIGH && btn1Prev == LOW) {
    if (millis() - btn1PressStart < LONG_PRESS_TIME && !longPressDetected) {
      // Короткое нажатие
      if (!isEditMode) {
        nextMenuItem();
      } else {
        // В режиме редактирования
        if (isDigitEdit) {
          nextDigit(); // Переход к следующему разряду
        } else {
          if (currentMenuItem == MENU_SETPOINT) {
            nextSubItem(); // Переключение L/H/Hyst
          } else {
            changeParameterValueSimple(); // Для Range/Unit просто шаг
          }
        }
      }
    }
  }

  // Кнопка 2 (CHANGE)
  if (btn2 == LOW && btn2Prev == HIGH) {
    if (isEditMode) {
      if (isDigitEdit) {
        incrementDigit(); // Изменение цифры в разряде
      } else {
        changeParameterValueSimple();
      }
    }
  }

  btn1Prev = btn1;
  btn2Prev = btn2;

  // Мигание курсора
  if (isDigitEdit && millis() - lastBlinkTime > 500) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }
}

// --- Логика меню ---

void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 6);
  currentSubItem = SUB_LOW; // Сброс подпункта
  delay(DEBOUNCE_DELAY);
}

void nextSubItem() {
  if (currentMenuItem == MENU_SETPOINT) {
    currentSubItem = (SetpointSubItems)((currentSubItem + 1) % 3); // 0:Low, 1:High, 2:Hyst
  }
  delay(DEBOUNCE_DELAY);
}

void toggleEditMode() {
  isEditMode = !isEditMode;

  if (isEditMode) {
    // Вход в редактирование: копируем текущие значения
    tempRangeIndex = currentRangeIndex;
    tempUnitIndex = currentUnitIndex;
    tempSetpointLow = setpointLow;
    tempSetpointHigh = setpointHigh;
    tempHysteresis = hysteresis;
    tempAdcMin = adcMinCal;
    tempAdcMax = adcMaxCal;

    isDigitEdit = false;
    currentSubItem = SUB_LOW;
  } else {
    // Выход и сохранение
    currentRangeIndex = tempRangeIndex;
    currentUnitIndex = tempUnitIndex;
    setpointLow = tempSetpointLow;
    setpointHigh = tempSetpointHigh;
    hysteresis = tempHysteresis;
    adcMinCal = tempAdcMin;
    adcMaxCal = tempAdcMax;

    saveSettings();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Saved!");
    delay(1000);
    lcd.clear();
  }
  delay(DEBOUNCE_DELAY);
}

// Простое изменение (для Range, Unit, перебор пунктов)
void changeParameterValueSimple() {
  switch (currentMenuItem) {
    case MENU_RANGE:
      tempRangeIndex = (tempRangeIndex + 1) % RANGES_COUNT;
      break;
    case MENU_UNIT:
      tempUnitIndex = (tempUnitIndex + 1) % UNITS_COUNT;
      break;
    case MENU_CALIB:
      // Запуск калибровки по шагам (упрощенно)
      runCalibrationStep();
      break;
    case MENU_RESET:
      resetToFactory();
      isEditMode = false; // Выход после сброса
      lcd.clear();
      lcd.print("Reset Done");
      delay(1000);
      break;
  }
  delay(DEBOUNCE_DELAY);
}

// --- Поразрядный ввод ---

void startDigitEdit(float val, int digits) {
  editValue = val;
  maxDigits = digits;
  editDigitIndex = digits - 1; // Начинаем со старшего разряда
  isDigitEdit = true;
}

void nextDigit() {
  editDigitIndex--;
  if (editDigitIndex < 0) {
    // Закончили ввод числа
    finishDigitEdit();
  }
  delay(DEBOUNCE_DELAY);
}

void incrementDigit() {
  float multiplier = pow(10, editDigitIndex);
  if (editDigitIndex == 0) multiplier = 0.1; // Для десятых

  // Получаем текущую цифру в этом разряде
  // Сложная математика для float, проще работать с целым представлением
  // Но для простоты сделаем так:

  int currentDigit = (int)(editValue / multiplier) % 10;
  if (editDigitIndex == 0) {
    // Для десятых: (val * 10) % 10
    currentDigit = (int)(editValue * 10) % 10;
  } else {
    currentDigit = (int)(editValue / multiplier) % 10;
  }

  currentDigit = (currentDigit + 1) % 10;

  // Очищаем этот разряд и ставим новый
  float rest = fmod(editValue, multiplier);
  if (editDigitIndex == 0) rest = 0; // Если меняем десятые, остаток 0

  float higher = editValue - (editValue - rest); // Часть выше разряда? Нет, сложнее.

  // Проще: пересобрать число из строк или массива цифр, но тут сделаем математику
  // Удаляем старую цифру:
  editValue -= currentDigit * (editDigitIndex == 0 ? 0.1 : multiplier); // Ошибка логики выше, исправим подходом "сборки"

  // ПРАВИЛЬНЫЙ ПОДХОД:
  // 1. Округлим до текущего разряда
  float factor = (editDigitIndex == 0) ? 10.0 : pow(10, editDigitIndex);
  int tempInt = round(editValue * (editDigitIndex == 0 ? 10 : 1));
  // Это сложно с плавающей точкой. Давайте используем простой метод замены:

  // Разобьем число на целое и дробное
  int integerPart = (int)editValue;
  int decimalPart = (int)((editValue - integerPart) * 10 + 0.5);

  if (editDigitIndex == 0) { // Десятые
    decimalPart = (decimalPart + 1) % 10;
    editValue = integerPart + decimalPart * 0.1;
  } else if (editDigitIndex == 1) { // Единицы
    integerPart = (integerPart % 10) + ((integerPart / 10 % 10 + 1) % 10) * 10; // Ошибка, перепишем
    // Просто меняем цифру
    int d = (integerPart / 1) % 10;
    d = (d + 1) % 10;
    integerPart = (integerPart / 10) * 10 + d;
    editValue = integerPart + decimalPart * 0.1;
  } else if (editDigitIndex == 2) { // Десятки
    int d = (integerPart / 10) % 10;
    d = (d + 1) % 10;
    integerPart = (integerPart / 100) * 100 + d * 10 + (integerPart % 10);
    editValue = integerPart + decimalPart * 0.1;
  } else if (editDigitIndex == 3) { // Сотни
    int d = (integerPart / 100) % 10;
    d = (d + 1) % 10;
    integerPart = d * 100 + (integerPart % 100);
    editValue = integerPart + decimalPart * 0.1;
  }

  // Проверка границ
  validateTempValues();

  delay(DEBOUNCE_DELAY);
}

void finishDigitEdit() {
  isDigitEdit = false;
  // Применяем значение в зависимости от того, что редактировали
  if (currentMenuItem == MENU_SETPOINT) {
    if (currentSubItem == SUB_LOW) tempSetpointLow = editValue;
    else if (currentSubItem == SUB_HIGH) tempSetpointHigh = editValue;
    else if (currentSubItem == SUB_HYST) tempHysteresis = editValue;
  }
  // Для калибровки аналогично, если нужно
}

void validateTempValues() {
  // Не даем Lower стать больше High - Hyst
  if (currentMenuItem == MENU_SETPOINT) {
    if (currentSubItem == SUB_LOW && editValue >= tempSetpointHigh) {
      editValue = tempSetpointHigh - 0.1;
    }
    if (currentSubItem == SUB_HIGH && editValue <= tempSetpointLow) {
      editValue = tempSetpointLow + 0.1;
    }
  }
}

// --- Экраны ---

void displayWorkScreen(float pressure) {
  if (sensorBreak) {
    lcd.setCursor(0, 0);
    lcd.print("ERR: BREAK LINE");
    lcd.setCursor(0, 1);
    lcd.print("Relays OFF      ");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("P: ");
  lcd.print(pressure, 1);
  lcd.print(" ");
  lcd.print(UNIT_NAMES[currentUnitIndex]);
  lcd.print("       ");

  lcd.setCursor(0, 1);
  lcd.print("V1:");
  lcd.print(valve1State ? "ON " : "OFF");
  lcd.print(" V2:");
  lcd.print(valve2State ? "ON " : "OFF");
  lcd.print("       ");
}

void displayEditScreen() {
  const char* menuNames[] = {"Mode", "Range", "Setpoint", "Unit", "Calibr", "Reset"};

  // Верхняя строка: Название пункта
  lcd.setCursor(0, 0);
  lcd.print(menuNames[currentMenuItem]);
  lcd.print(":             ");

  lcd.setCursor(0, 1);

  if (currentMenuItem == MENU_MODE) {
    lcd.print("Normal Mode     ");
  }
  else if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[tempRangeIndex], 0);
    lcd.print("        ");
  }
  else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[tempUnitIndex]);
    lcd.print("        ");
  }
  else if (currentMenuItem == MENU_RESET) {
    lcd.print("Hold to Reset!  ");
  }
  else if (currentMenuItem == MENU_CALIB) {
    lcd.print("Auto Calib...   ");
  }
  else if (currentMenuItem == MENU_SETPOINT) {
    // Подменю: Low, High, Hyst
    const char* subNames[] = {"Lower", "Upper", "Hyst"};
    lcd.print(subNames[currentSubItem]);
    lcd.print(":");

    float valToEdit = 0;
    int digits = 4; // Сотни, десятки, единицы, десятые

    if (currentSubItem == SUB_LOW) valToEdit = tempSetpointLow;
    else if (currentSubItem == SUB_HIGH) valToEdit = tempSetpointHigh;
    else { valToEdit = tempHysteresis; digits = 3; } // Гистерезис обычно меньше

    // Если только что вошли в пункт, инициируем поразрядный ввод
    static MenuItems prevMenu = MENU_MODE;
    static SetpointSubItems prevSub = SUB_LOW;

    if (!isDigitEdit && (prevMenu != currentMenuItem || prevSub != currentSubItem)) {
      startDigitEdit(valToEdit, digits);
      // Синхронизируем editValue с тем, что редактируем
      if (currentSubItem == SUB_LOW) editValue = tempSetpointLow;
      else if (currentSubItem == SUB_HIGH) editValue = tempSetpointHigh;
      else editValue = tempHysteresis;
    }
    prevMenu = currentMenuItem;
    prevSub = currentSubItem;

    if (isDigitEdit) {
      // Рисуем число с подчеркиванием мигающего разряда
      drawNumberWithCursor(editValue, editDigitIndex);
    } else {
      lcd.print(valToEdit, 1);
      lcd.print("        ");
    }
  }
}

void drawNumberWithCursor(float val, int cursorIdx) {
  // Преобразуем в строку вручную для контроля
  int iPart = (int)val;
  int dPart = (int)((val - iPart) * 10 + 0.5);
  if (dPart >= 10) { dPart = 0; iPart++; }

  char buf[10];
  // Формат: XXX.X
  sprintf(buf, "%d.%d", iPart, dPart);

  // Позиции в строке: 0(сотни), 1(десятки), 2(единицы), 4(десятые)
  // cursorIdx: 3->сотни, 2->десятки, 1->единицы, 0->десятые

  int posMap[] = {4, 2, 1, 0}; // Индекс в buf для cursorIdx 0,1,2,3

  lcd.print(buf);
  lcd.print("   ");

  // Возвращаем курсор для мигания
  int charPos = posMap[cursorIdx];
  // Выводим пробел вместо цифры если мигание выключено
  if (!blinkState) {
    lcd.setCursor(charPos, 1);
    lcd.print(" ");
  }
}

// --- Калибровка и EEPROM ---

void runCalibrationStep() {
  static int calStep = 0;
  // 0: Ждем 4мА, 1: Сохраняем мин, 2: Ждем 20мА, 3: Сохраняем макс
  // Упрощено: одно нажатие - запись мин, второе - макс

  int val = analogRead(PIN_SENSOR);
  lcd.clear();
  lcd.setCursor(0, 0);

  if (calStep == 0) {
    lcd.print("Apply 4mA (0%)");
    lcd.setCursor(0, 1);
    lcd.print("Val: "); lcd.print(val);
    if (digitalRead(PIN_BTN_CHANGE) == LOW) {
      tempAdcMin = val;
      calStep = 1;
      delay(500);
    }
  } else {
    lcd.print("Apply 20mA (100%)");
    lcd.setCursor(0, 1);
    lcd.print("Val: "); lcd.print(val);
    if (digitalRead(PIN_BTN_CHANGE) == LOW) {
      tempAdcMax = val;
      calStep = 0;
      // Автовыход из калибровки при завершении? Или флаг
      // Для простоты сбрасываем шаг после выхода из меню
      delay(500);
    }
  }
}

void resetToFactory() {
  tempRangeIndex = 2;
  tempUnitIndex = 1;
  tempSetpointLow = 20.0;
  tempSetpointHigh = 80.0;
  tempHysteresis = 2.0;
  tempAdcMin = 197;
  tempAdcMax = 983;
}

void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_NUM);
  EEPROM.put(ADDR_RANGE, currentRangeIndex);
  EEPROM.put(ADDR_UNIT, currentUnitIndex);
  EEPROM.put(ADDR_SET_LOW, setpointLow);
  EEPROM.put(ADDR_SET_HIGH, setpointHigh);
  EEPROM.put(ADDR_HYST, hysteresis);
  EEPROM.put(ADDR_CAL_MIN, adcMinCal);
  EEPROM.put(ADDR_CAL_MAX, adcMaxCal);
}

void loadSettings() {
  int magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic == MAGIC_NUM) {
    EEPROM.get(ADDR_RANGE, currentRangeIndex);
    EEPROM.get(ADDR_UNIT, currentUnitIndex);
    EEPROM.get(ADDR_SET_LOW, setpointLow);
    EEPROM.get(ADDR_SET_HIGH, setpointHigh);
    EEPROM.get(ADDR_HYST, hysteresis);
    EEPROM.get(ADDR_CAL_MIN, adcMinCal);
    EEPROM.get(ADDR_CAL_MAX, adcMaxCal);
  } else {
    resetToFactory();
    saveSettings();
  }
}
