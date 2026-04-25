/*
 * Pressure Controller for Arduino Nano (v2.0 - Digit-by-Digit Input)
 *
 * Функционал:
 * - Чтение датчика 4-20мА (A0) с калибровкой.
 * - Управление 2 реле (D2, D3) по уставкам с настраиваемым гистерезисом.
 * - Дисплей 1602 I2C.
 * - Меню: Режим, Ранг, Уставки (L/H), Гистерезис, Ед. Изм., Калибровка, Сброс.
 * - Портазрядный ввод чисел (мигающий курсор).
 * - Сохранение в EEPROM.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// --- Пиновка ---
const int PIN_SENSOR = A0;
const int PIN_RELAY_1 = 2;
const int PIN_RELAY_2 = 3;
const int PIN_BTN_MENU = 4;
const int PIN_BTN_CHANGE = 5;

// --- Дисплей ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Константы ---
const int RANGES_COUNT = 6;
const float RANGES[RANGES_COUNT] = {1.0, 40.0, 250.0, 400.0, 600.0, 1000.0};
const char* UNIT_NAMES[] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const int UNITS_COUNT = 5;

// Адреса EEPROM
const int ADDR_MAGIC = 0;
const int ADDR_RANGE = 1;
const int ADDR_UNIT = 2;
const int ADDR_SP_LOW_H = 3; // High byte
const int ADDR_SP_LOW_L = 4; // Low byte
const int ADDR_SP_HIGH_H = 5;
const int ADDR_SP_HIGH_L = 6;
const int ADDR_HYST_H = 7;
const int ADDR_HYST_L = 8;
const int ADDR_CAL_MIN_H = 9;
const int ADDR_CAL_MIN_L = 10;
const int ADDR_CAL_MAX_H = 11;
const int ADDR_CAL_MAX_L = 12;

const unsigned long MAGIC_NUM = 12345;

// --- Глобальные переменные ---
int currentRangeIndex = 2;
int currentUnitIndex = 1;
float setpointLow = 20.0;
float setpointHigh = 80.0;
float hysteresis = 2.0;

// Калибровка (ADC значения)
int calMin = 197; // 4mA
int calMax = 983; // 20mA

bool valve1State = false;
bool valve2State = false;
bool sensorError = false;

// --- Меню ---
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_HYST, MENU_UNIT, MENU_CALIB, MENU_RESET };
MenuItems currentMenuItem = MENU_MODE;
bool isEditMode = false;
bool isDigitEditMode = false; // Режим поразрядного ввода

// Для поразрядного ввода
float tempEditValue = 0.0;
int digitIndex = 0; // 0=сотни, 1=десятки, 2=единицы, 3=десятые
int digits[4] = {0, 0, 0, 0};
unsigned long lastBtnTime = 0;
const unsigned long DEBOUNCE = 200;
const unsigned long LONG_PRESS = 1500;

// Для выбора уставки (L/H) внутри пункта Setpoint
bool selectLowSetpoint = true;

// Для калибровки
int calStep = 0; // 0=ничего, 1=калибровка мин, 2=калибровка макс

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
  lcd.clear();
  lcd.print("Pressure Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");

  loadSettings();
  delay(1000);
  lcd.clear();
}

void loop() {
  handleButtons();

  if (!isEditMode && !isDigitEditMode) {
    // Рабочий режим
    float pressure = readPressure();
    controlRelays(pressure);
    displayWorkScreen(pressure);
  } else if (isDigitEditMode) {
    displayDigitEditScreen();
  } else {
    displayMenuScreen();
  }
}

// --- Чтение и Калибровка ---
float readPressure() {
  int val = analogRead(PIN_SENSOR);

  // Проверка обрыва (< 4мА примерно соответствует val < calMin - запас)
  if (val < (calMin - 20)) {
    sensorError = true;
    return 0.0;
  }
  sensorError = false;

  if (val < calMin) val = calMin;
  if (val > calMax) val = calMax;

  float percent = (float)(val - calMin) / (float)(calMax - calMin);
  float rangeVal = RANGES[currentRangeIndex];
  return percent * rangeVal;
}

void controlRelays(float p) {
  if (sensorError) {
    valve1State = false;
    valve2State = false;
    digitalWrite(PIN_RELAY_1, LOW);
    digitalWrite(PIN_RELAY_2, LOW);
    return;
  }

  // Реле 1 (Низкое)
  if (p < (setpointLow - hysteresis)) valve1State = true;
  if (p > setpointLow) valve1State = false;

  // Реле 2 (Высокое)
  if (p > (setpointHigh + hysteresis)) valve2State = true;
  if (p < setpointHigh) valve2State = false;

  digitalWrite(PIN_RELAY_1, valve1State ? HIGH : LOW);
  digitalWrite(PIN_RELAY_2, valve2State ? HIGH : LOW);
}

// --- Кнопки ---
void handleButtons() {
  static bool b1Prev = HIGH, b2Prev = HIGH;
  static unsigned long pressStart = 0;
  static bool longPressed = false;

  bool b1 = digitalRead(PIN_BTN_MENU);
  bool b2 = digitalRead(PIN_BTN_CHANGE);
  unsigned long now = millis();

  if (now - lastBtnTime < DEBOUNCE) return;

  // Кнопка MENU
  if (b1 == LOW && b1Prev == HIGH) {
    pressStart = now;
    longPressed = false;
  }
  if (b1 == LOW && !longPressed) {
    if (now - pressStart >= LONG_PRESS) {
      longPressed = true;
      handleLongPress();
    }
  }
  if (b1 == HIGH && b1Prev == LOW && !longPressed) {
    handleShortPress();
  }

  // Кнопка CHANGE
  if (b2 == LOW && b2Prev == HIGH) {
    handleChangePress();
  }

  b1Prev = b1;
  b2Prev = b2;
}

void handleLongPress() {
  lastBtnTime = millis();
  if (isDigitEditMode) {
    // Отмена редактирования числа
    isDigitEditMode = false;
    isEditMode = false;
    lcd.clear();
  } else if (isEditMode) {
    // Выход из меню без сохранения (если не в цифровом режиме)
    isEditMode = false;
    lcd.clear();
  } else {
    // Вход в редактирование текущего пункта
    enterEditMode();
  }
}

void handleShortPress() {
  lastBtnTime = millis();
  if (isDigitEditMode) {
    nextDigit();
  } else if (isEditMode) {
    if (currentMenuItem == MENU_SETPOINT) {
      // Переключение L/H
      selectLowSetpoint = !selectLowSetpoint;
    } else {
      nextMenuItem();
    }
  } else {
    nextMenuItem();
  }
}

void handleChangePress() {
  lastBtnTime = millis();
  if (isDigitEditMode) {
    incrementDigit();
  } else if (isEditMode) {
    incrementValue();
  }
}

// --- Логика Меню ---
void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 7); // 7 пунктов
  // Пропуск калибровки если не нужно? Нет, оставим все.
}

void enterEditMode() {
  isEditMode = true;
  if (currentMenuItem == MENU_SETPOINT || currentMenuItem == MENU_HYST) {
    startDigitEdit();
  } else {
    // Для списков (Range, Unit) просто вход, изменение кнопкой Change
    if (currentMenuItem == MENU_RESET) {
      resetSettings();
      isEditMode = false;
      lcd.clear();
      lcd.print("Reset Done!");
      delay(1000);
    }
    if (currentMenuItem == MENU_CALIB) {
      startCalibration();
      isEditMode = false; // Калибровка свой процесс
    }
  }
}

// --- Портазрядный ввод ---
void startDigitEdit() {
  isDigitEditMode = true;
  digitIndex = 0;

  float val = 0;
  if (currentMenuItem == MENU_SETPOINT) {
    val = selectLowSetpoint ? setpointLow : setpointHigh;
  } else if (currentMenuItem == MENU_HYST) {
    val = hysteresis;
  }

  // Разбор числа на цифры (предполагаем формат XXX.X)
  // Ограничим макс значением ранга
  float maxVal = RANGES[currentRangeIndex];
  if (val > maxVal) val = maxVal;

  int iVal = (int)(val * 10 + 0.5); // Перевод в десятые (например 25.5 -> 255)

  digits[3] = iVal % 10;       // Десятые
  digits[2] = (iVal / 10) % 10; // Единицы
  digits[1] = (iVal / 100) % 10; // Десятки
  digits[0] = (iVal / 1000) % 10; // Сотни

  tempEditValue = val;
}

void nextDigit() {
  digitIndex++;
  if (digitIndex > 3) {
    saveDigitEdit();
    isDigitEditMode = false;
    // После сохранения числа выходим из режима редактирования меню или переходим дальше?
    // Лучше остаться в меню, чтобы можно было выйти длинным нажатием
    isEditMode = false;
    lcd.clear();
    lcd.print("Saved!");
    delay(800);
    lcd.clear();
  }
}

void incrementDigit() {
  digits[digitIndex]++;
  if (digits[digitIndex] > 9) digits[digitIndex] = 0;

  // Сборка числа обратно для проверки границ
  int total = digits[0]*1000 + digits[1]*100 + digits[2]*10 + digits[3];
  float newVal = (float)total / 10.0;

  float limit = RANGES[currentRangeIndex];
  if (currentMenuItem == MENU_HYST) limit = limit / 2.0; // Гистерезис не больше половины диапазона

  // Простая проверка: если превысили лимит, обнуляем старшие разряды или запрещаем?
  // Для простоты: если число > лимита, сбрасываем в 0 или не даем увеличить?
  // Реализуем "перенос": если > лимита, то ставим 0.0
  if (newVal > limit) {
    // Сброс всех цифр в 0
    for(int i=0; i<4; i++) digits[i] = 0;
  }
}

void saveDigitEdit() {
  int total = digits[0]*1000 + digits[1]*100 + digits[2]*10 + digits[3];
  float newVal = (float)total / 10.0;

  if (currentMenuItem == MENU_SETPOINT) {
    if (selectLowSetpoint) {
      setpointLow = newVal;
      if (setpointLow >= setpointHigh) setpointHigh = setpointLow + 1.0;
    } else {
      setpointHigh = newVal;
      if (setpointHigh <= setpointLow) setpointLow = setpointHigh - 1.0;
    }
  } else if (currentMenuItem == MENU_HYST) {
    hysteresis = newVal;
  }
  saveSettings();
}

void displayDigitEditScreen() {
  lcd.clear();
  const char* label = "";
  if (currentMenuItem == MENU_SETPOINT) label = selectLowSetpoint ? "Set L:" : "Set H:";
  else if (currentMenuItem == MENU_HYST) label = "Hyst:";

  lcd.print(label);
  lcd.setCursor(0, 1);

  for (int i = 0; i < 4; i++) {
    if (i == 2) lcd.print(".");
    if (i == digitIndex) {
      lcd.print(digits[i]);
      lcd.noDisplay(); // Мигание: выключаем дисплей на время? Нет, лучше инверсия или пробел
      // LiquidCrystal_I2C не поддерживает инверсию символа легко.
      // Сделаем так: печатаем цифру, потом стираем и печатаем снова в цикле? Слишком сложно.
      // Простой вариант: печатаем цифру, а соседние как есть.
      // Эмуляция мигания через стирание:
      if ((millis() / 500) % 2 == 0) {
        lcd.print(" ");
      } else {
        lcd.print(digits[i]);
      }
    } else {
      lcd.print(digits[i]);
    }
  }
}

// --- Обычное редактирование (списки) ---
void incrementValue() {
  if (currentMenuItem == MENU_RANGE) {
    currentRangeIndex = (currentRangeIndex + 1) % RANGES_COUNT;
    saveSettings();
  } else if (currentMenuItem == MENU_UNIT) {
    currentUnitIndex = (currentUnitIndex + 1) % UNITS_COUNT;
    saveSettings();
  }
}

void displayMenuScreen() {
  lcd.clear();
  const char* names[] = {"Mode", "Range", "Setpoint", "Hyst", "Unit", "Calibr", "Reset"};
  lcd.print(names[currentMenuItem]);
  lcd.setCursor(0, 1);

  if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[currentRangeIndex], 0);
    lcd.print(" (Change)");
  } else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[currentUnitIndex]);
    lcd.print(" (Change)");
  } else if (currentMenuItem == MENU_SETPOINT) {
    lcd.print(selectLowSetpoint ? "Low" : "High");
    lcd.print(" Setpoint");
  } else if (currentMenuItem == MENU_HYST) {
    lcd.print(hysteresis, 1);
    lcd.print(" (Edit)");
  } else if (currentMenuItem == MENU_CALIB) {
    lcd.print("Press Enter");
  } else if (currentMenuItem == MENU_RESET) {
    lcd.print("Hold to Reset");
  } else {
    lcd.print("Normal Mode");
  }
}

void displayWorkScreen(float p) {
  if (sensorError) {
    lcd.setCursor(0, 0);
    lcd.print("ERR: BREAK LINE");
    lcd.setCursor(0, 1);
    lcd.print("Check Sensor!   ");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("P:");
  lcd.print(p, 1);
  lcd.print(" ");
  lcd.print(UNIT_NAMES[currentUnitIndex]);
  lcd.print("      ");

  lcd.setCursor(0, 1);
  lcd.print("L:");
  lcd.print(valve1State ? "ON " : "OFF");
  lcd.print(" H:");
  lcd.print(valve2State ? "ON " : "OFF");
  lcd.print("    ");
}

// --- EEPROM ---
void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_NUM);
  EEPROM.put(ADDR_RANGE, currentRangeIndex);
  EEPROM.put(ADDR_UNIT, currentUnitIndex);

  int iLow = (int)(setpointLow * 10);
  int iHigh = (int)(setpointHigh * 10);
  int iHyst = (int)(hysteresis * 10);

  EEPROM.put(ADDR_SP_LOW_H, highByte(iLow));
  EEPROM.put(ADDR_SP_LOW_L, lowByte(iLow));
  EEPROM.put(ADDR_SP_HIGH_H, highByte(iHigh));
  EEPROM.put(ADDR_SP_HIGH_L, lowByte(iHigh));
  EEPROM.put(ADDR_HYST_H, highByte(iHyst));
  EEPROM.put(ADDR_HYST_L, lowByte(iHyst));

  EEPROM.put(ADDR_CAL_MIN_H, highByte(calMin));
  EEPROM.put(ADDR_CAL_MIN_L, lowByte(calMin));
  EEPROM.put(ADDR_CAL_MAX_H, highByte(calMax));
  EEPROM.put(ADDR_CAL_MAX_L, lowByte(calMax));
}

void loadSettings() {
  unsigned long magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic != MAGIC_NUM) {
    resetSettings();
    return;
  }

  EEPROM.get(ADDR_RANGE, currentRangeIndex);
  EEPROM.get(ADDR_UNIT, currentUnitIndex);

  byte h, l;
  EEPROM.get(ADDR_SP_LOW_H, h); EEPROM.get(ADDR_SP_LOW_L, l);
  setpointLow = (float)(word(h, l)) / 10.0;

  EEPROM.get(ADDR_SP_HIGH_H, h); EEPROM.get(ADDR_SP_HIGH_L, l);
  setpointHigh = (float)(word(h, l)) / 10.0;

  EEPROM.get(ADDR_HYST_H, h); EEPROM.get(ADDR_HYST_L, l);
  hysteresis = (float)(word(h, l)) / 10.0;

  EEPROM.get(ADDR_CAL_MIN_H, h); EEPROM.get(ADDR_CAL_MIN_L, l);
  calMin = word(h, l);

  EEPROM.get(ADDR_CAL_MAX_H, h); EEPROM.get(ADDR_CAL_MAX_L, l);
  calMax = word(h, l);

  if (calMin >= calMax) { calMin = 197; calMax = 983; }
}

void resetSettings() {
  currentRangeIndex = 2;
  currentUnitIndex = 1;
  setpointLow = 20.0;
  setpointHigh = 80.0;
  hysteresis = 2.0;
  calMin = 197;
  calMax = 983;
  EEPROM.clear();
  saveSettings();
}

// --- Калибровка ---
void startCalibration() {
  lcd.clear();
  lcd.print("Calibration");
  lcd.setCursor(0, 1);
  lcd.print("Apply 4mA...");
  delay(2000);

  int minVal = analogRead(PIN_SENSOR);
  // Ждем стабилизации (упрощенно)
  for(int i=0; i<50; i++) {
    int v = analogRead(PIN_SENSOR);
    if(v < minVal) minVal = v;
    delay(20);
  }
  calMin = minVal;

  lcd.clear();
  lcd.print("Apply 20mA...");
  delay(2000);

  int maxVal = analogRead(PIN_SENSOR);
  for(int i=0; i<50; i++) {
    int v = analogRead(PIN_SENSOR);
    if(v > maxVal) maxVal = v;
    delay(20);
  }
  calMax = maxVal;

  if (calMax <= calMin) {
    lcd.clear();
    lcd.print("Error Cal!");
    delay(2000);
    calMin = 197; calMax = 983;
  } else {
    lcd.clear();
    lcd.print("Calib OK!");
    delay(1000);
  }
  saveSettings();
  lcd.clear();
}
