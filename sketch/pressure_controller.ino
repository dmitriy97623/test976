/*
 * Pressure Controller for Arduino Nano (Advanced)
 *
 * Функционал:
 * - Чтение датчика 4-20мА с калибровкой и проверкой обрыва
 * - Управление 2 реле с гистерезисом (настраиваемым)
 * - Дисплей 1602 I2C
 * - Меню: Режим, Ранг, Ед. Изм., Уставки, Гистерезис, Калибровка, Сброс
 * - Сохранение всех настроек в EEPROM
 * - Навигация: Длинное нажатие (Enter/Exit), Короткое (Next/Change)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// --- Конфигурация пинов ---
const int PIN_SENSOR = A0;
const int PIN_RELAY_1 = 2;
const int PIN_RELAY_2 = 3;
const int PIN_BTN_MENU = 4;
const int PIN_BTN_CHANGE = 5;

// --- Настройки дисплея ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Адреса EEPROM ---
#define ADDR_MAGIC 0
#define ADDR_RANGE 2
#define ADDR_UNIT 4
#define ADDR_SET_LOW 6
#define ADDR_SET_HIGH 10
#define ADDR_HYST 14
#define ADDR_CAL_MIN 18
#define ADDR_CAL_MAX 22

#define MAGIC_VALUE 1234

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

// Калибровка (значения ADC)
int calMinADC = 197;
int calMaxADC = 983;

bool valve1State = false;
bool valve2State = false;
bool sensorError = false; // Обрыв цепи

// --- Переменные меню ---
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_UNIT, MENU_SETPOINT, MENU_HYST, MENU_CALIB, MENU_RESET };
MenuItems currentMenuItem = MENU_MODE;
bool isEditMode = false;
unsigned long btnPressStart = 0;
const unsigned long LONG_PRESS_TIME = 1500;
const unsigned long DEBOUNCE_DELAY = 300;

// Временные переменные
int tempRangeIndex = 0;
int tempUnitIndex = 0;
float tempSetpointLow = 0;
float tempSetpointHigh = 0;
float tempHyst = 0;
int tempCalMin = 0;
int tempCalMax = 0;

bool setpointFocusLow = true;
bool calibFocusMin = true;

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
  lcd.print("Ver 2.0 EEPROM");
  delay(1500);
  lcd.clear();
}

void loop() {
  static unsigned long lastLoopTime = 0;
  if (millis() - lastLoopTime < 100) return;
  lastLoopTime = millis();

  handleButtons();

  if (!isEditMode) {
    float pressureValue = readPressure();
    if (!sensorError) {
      controlRelays(pressureValue);
    } else {
      updateRelays(false, false); // Отключаем реле при ошибке
    }
    displayWorkScreen(pressureValue);
  } else {
    displayEditScreen();
  }
}

// --- EEPROM ---
void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_VALUE);
  EEPROM.put(ADDR_RANGE, currentRangeIndex);
  EEPROM.put(ADDR_UNIT, currentUnitIndex);
  EEPROM.put(ADDR_SET_LOW, setpointLow);
  EEPROM.put(ADDR_SET_HIGH, setpointHigh);
  EEPROM.put(ADDR_HYST, hysteresis);
  EEPROM.put(ADDR_CAL_MIN, calMinADC);
  EEPROM.put(ADDR_CAL_MAX, calMaxADC);
}

void loadSettings() {
  int magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic == MAGIC_VALUE) {
    EEPROM.get(ADDR_RANGE, currentRangeIndex);
    EEPROM.get(ADDR_UNIT, currentUnitIndex);
    EEPROM.get(ADDR_SET_LOW, setpointLow);
    EEPROM.get(ADDR_SET_HIGH, setpointHigh);
    EEPROM.get(ADDR_HYST, hysteresis);
    EEPROM.get(ADDR_CAL_MIN, calMinADC);
    EEPROM.get(ADDR_CAL_MAX, calMaxADC);
  } else {
    // Заводские настройки
    resetToDefaults();
  }
}

void resetToDefaults() {
  currentRangeIndex = 2;
  currentUnitIndex = 1;
  setpointLow = 20.0;
  setpointHigh = 80.0;
  hysteresis = 2.0;
  calMinADC = 197;
  calMaxADC = 983;
  saveSettings();
}

// --- Чтение датчика ---
float readPressure() {
  int sensorValue = analogRead(PIN_SENSOR);

  // Проверка обрыва (меньше минимального калиброванного значения с запасом)
  if (sensorValue < (calMinADC - 20)) {
    sensorError = true;
    return 0.0;
  }
  sensorError = false;

  if (sensorValue < calMinADC) sensorValue = calMinADC;
  if (sensorValue > calMaxADC) sensorValue = calMaxADC;

  float percent = (float)(sensorValue - calMinADC) / (float)(calMaxADC - calMinADC) * 100.0;
  float rangeVal = RANGES[currentRangeIndex];
  return (percent / 100.0) * rangeVal;
}

// --- Управление реле ---
void controlRelays(float pressure) {
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

// --- Кнопки ---
void handleButtons() {
  static bool btn1Prev = HIGH;
  static bool btn2Prev = HIGH;
  static bool longPressDetected = false;

  bool btn1 = digitalRead(PIN_BTN_MENU);
  bool btn2 = digitalRead(PIN_BTN_CHANGE);

  // Кнопка 1 (Menu)
  if (btn1 == LOW && btn1Prev == HIGH) {
    btnPressStart = millis();
    longPressDetected = false;
  }

  if (btn1 == LOW && !longPressDetected) {
    if (millis() - btnPressStart >= LONG_PRESS_TIME) {
      longPressDetected = true;
      toggleEditMode();
    }
  }

  if (btn1 == HIGH && btn1Prev == LOW && !longPressDetected) {
    if (!isEditMode) {
      nextMenuItem();
    } else {
      cycleFocus();
    }
  }

  // Кнопка 2 (Change)
  if (btn2 == LOW && btn2Prev == HIGH) {
    if (isEditMode) {
      changeParameterValue();
    }
  }

  btn1Prev = btn1;
  btn2Prev = btn2;
}

void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 7); // 7 пунктов
  delay(DEBOUNCE_DELAY);
}

void cycleFocus() {
  if (currentMenuItem == MENU_SETPOINT) {
    setpointFocusLow = !setpointFocusLow;
  } else if (currentMenuItem == MENU_CALIB) {
    calibFocusMin = !calibFocusMin;
  }
  delay(DEBOUNCE_DELAY);
}

void toggleEditMode() {
  isEditMode = !isEditMode;
  if (isEditMode) {
    // Загрузка временных значений
    tempRangeIndex = currentRangeIndex;
    tempUnitIndex = currentUnitIndex;
    tempSetpointLow = setpointLow;
    tempSetpointHigh = setpointHigh;
    tempHyst = hysteresis;
    tempCalMin = calMinADC;
    tempCalMax = calMaxADC;
    setpointFocusLow = true;
    calibFocusMin = true;
  } else {
    // Сохранение
    currentRangeIndex = tempRangeIndex;
    currentUnitIndex = tempUnitIndex;
    setpointLow = tempSetpointLow;
    setpointHigh = tempSetpointHigh;
    hysteresis = tempHyst;
    // Калибровку сохраняем сразу, если изменили? Или тоже по выходу?
    // Для безопасности калибровки лучше сохранять сразу при изменении, но здесь сделаем по выходу
    calMinADC = tempCalMin;
    calMaxADC = tempCalMax;

    saveSettings();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Saved!");
    delay(1000);
    lcd.clear();
  }
  delay(DEBOUNCE_DELAY);
}

void changeParameterValue() {
  switch (currentMenuItem) {
    case MENU_RANGE:
      tempRangeIndex = (tempRangeIndex + 1) % RANGES_COUNT;
      break;
    case MENU_UNIT:
      tempUnitIndex = (tempUnitIndex + 1) % UNITS_COUNT;
      break;
    case MENU_SETPOINT:
      if (setpointFocusLow) {
        tempSetpointLow += 1.0;
        if (tempSetpointLow > tempSetpointHigh) tempSetpointLow = tempSetpointHigh;
      } else {
        tempSetpointHigh += 1.0;
        if (tempSetpointHigh > RANGES[tempRangeIndex]) tempSetpointHigh = RANGES[tempRangeIndex];
      }
      break;
    case MENU_HYST:
      tempHyst += 0.5;
      if (tempHyst > 10.0) tempHyst = 0.5;
      break;
    case MENU_CALIB:
      // В режиме калибровки кнопка меняет режим (Min/Max), а значение берется текущее с датчика
      // Логика изменена: вход в подменю калибровки выбирает что калибруем,
      // а сохранение происходит при переключении или выходе?
      // Упростим: Кнопка Change фиксирует текущее значение ADC для выбранного пункта (Min или Max)
      if (calibFocusMin) {
        tempCalMin = analogRead(PIN_SENSOR);
      } else {
        tempCalMax = analogRead(PIN_SENSOR);
      }
      // Визуальный фидбек можно добавить в дисплей
      break;
    case MENU_RESET:
      // Длинное нажатие на Change в пункте Reset выполнит сброс?
      // Или просто короткое? Сделаем так: короткое спрашивает подтверждение (упростим: сразу сброс)
      resetToDefaults();
      isEditMode = false; // Выход
      lcd.clear();
      lcd.print("Reset Done!");
      delay(2000);
      lcd.clear();
      break;
    case MENU_MODE:
      break;
  }
  delay(DEBOUNCE_DELAY);
}

// --- Экраны ---
void displayWorkScreen(float pressure) {
  lcd.setCursor(0, 0);
  if (sensorError) {
    lcd.print("ERR: OPEN CIRCUIT");
  } else {
    lcd.print("P: ");
    lcd.print(pressure, 1);
    lcd.print(" ");
    lcd.print(UNIT_NAMES[currentUnitIndex]);
    lcd.print("       ");
  }

  lcd.setCursor(0, 1);
  lcd.print("V1:");
  lcd.print(valve1State ? "ON " : "OFF");
  lcd.print(" V2:");
  lcd.print(valve2State ? "ON " : "OFF");
  lcd.print("       ");
}

void displayEditScreen() {
  const char* menuNames[] = {"Mode", "Range", "Unit", "Set/Hys", "Calibr", "Reset"};

  lcd.setCursor(0, 0);
  lcd.print(menuNames[currentMenuItem]);
  lcd.print(":             ");

  lcd.setCursor(0, 1);

  if (currentMenuItem == MENU_MODE) {
    lcd.print("Normal Mode     ");
  } else if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[tempRangeIndex], 0);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[tempUnitIndex]);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_SETPOINT) {
    if (setpointFocusLow) {
      lcd.print(">L:"); lcd.print(tempSetpointLow, 1);
    } else {
      lcd.print(" H:"); lcd.print(tempSetpointHigh, 1);
    }
    lcd.print(" Hy:"); lcd.print(tempHyst, 1);
  } else if (currentMenuItem == MENU_HYST) {
    lcd.print("Hyst: ");
    lcd.print(tempHyst, 1);
    lcd.print("         ");
  } else if (currentMenuItem == MENU_CALIB) {
    if (calibFocusMin) {
      lcd.print(">Min: "); lcd.print(tempCalMin);
    } else {
      lcd.print(" Max: "); lcd.print(tempCalMax);
    }
    lcd.print(" Press Chng");
  } else if (currentMenuItem == MENU_RESET) {
    lcd.print("Press Chng!!  ");
  }
}
