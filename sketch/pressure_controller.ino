/*
 * Pressure Controller for Arduino Nano (v2.0 - Digit-by-Digit Input)
 *
 * Функционал:
 * - Чтение датчика 4-20мА (A0) с калибровкой.
 * - Управление 2 реле (D2, D3) с настраиваемым гистерезисом.
 * - Дисплей 1602 I2C.
 * - Портазрядный ввод чисел (Уставки, Гистерезис).
 * - EEPROM для сохранения всех настроек.
 * - Диагностика обрыва цепи.
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

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- EEPROM Адреса ---
#define ADDR_MAGIC 0
#define ADDR_RANGE 2
#define ADDR_UNIT 4
#define ADDR_SP_LOW 6
#define ADDR_SP_HIGH 10
#define ADDR_HYST 14
#define ADDR_CAL_MIN 18
#define ADDR_CAL_MAX 22

#define MAGIC_NUM 12345

// --- Константы ---
const int RANGES_COUNT = 6;
const float RANGES[] = {1.0, 40.0, 250.0, 400.0, 600.0, 1000.0};

const char* UNIT_NAMES[] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const int UNITS_COUNT = 5;

// --- Глобальные переменные ---
int currentRangeIndex = 2;
int currentUnitIndex = 1;
float setpointLow = 20.0;
float setpointHigh = 80.0;
float hysteresis = 2.0;

// Калибровка (ADC значения для 0% и 100%)
int calMinADC = 197;
int calMaxADC = 983;

bool valve1State = false;
bool valve2State = false;
bool sensorError = false;

// --- Меню и Состояния ---
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_HYST, MENU_UNIT, MENU_CALIB, MENU_RESET };
MenuItems currentMenuItem = MENU_MODE;

enum SetpointSub { SP_LOW, SP_HIGH };
SetpointSub spSubItem = SP_LOW;

bool isEditMode = false;      // Режим редактирования пункта (выбор из списка или вход в числовой ввод)
bool isDigitEdit = false;     // Режим поразрядного ввода числа
int digitIndex = 0;           // Текущий разряд (0-сотни, 1-десятки, 2-единицы, 3-десятые)
float tempEditValue = 0.0;    // Временное значение при редактировании

// Таймеры и флаги кнопок
unsigned long lastBtnTime = 0;
const unsigned long DEBOUNCE = 300;
const unsigned long LONG_PRESS = 2000;
bool longPressDetected = false;

// --- Прототипы ---
void loadSettings();
void saveSettings();
void resetSettings();
float readPressure();
void controlRelays(float pressure);
void handleButtons();
void displayScreen();
void startDigitEdit(float val);
float finishDigitEdit();

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

  lcd.setCursor(0, 0);
  lcd.print("Pressure Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Ver 2.0 Load...");
  delay(1500);
  lcd.clear();
}

void loop() {
  static unsigned long lastLoop = 0;
  if (millis() - lastLoop < 100) return;
  lastLoop = millis();

  handleButtons();

  if (!isEditMode && !isDigitEdit) {
    float p = readPressure();
    controlRelays(p);
    displayScreen(); // Основной экран или меню выбора
  } else if (isDigitEdit) {
    displayDigitEdit(); // Экран поразрядного ввода
  } else {
    displayListEdit(); // Экран редактирования списков (Ранг, Единицы)
  }
}

// --- Логика Кнопок ---
void handleButtons() {
  static bool b1Prev = HIGH, b2Prev = HIGH;
  static unsigned long b1Start = 0;

  bool b1 = digitalRead(PIN_BTN_MENU);
  bool b2 = digitalRead(PIN_BTN_CHANGE);

  // Кнопка MENU (B1)
  if (b1 == LOW && b1Prev == HIGH) {
    b1Start = millis();
    longPressDetected = false;
  }

  if (b1 == LOW && !longPressDetected && millis() - b1Start >= LONG_PRESS) {
    longPressDetected = true;
    // Длинное нажатие
    if (isDigitEdit) {
      // Отмена числового ввода
      isDigitEdit = false;
      isEditMode = false;
      lcd.clear();
    } else if (isEditMode) {
      // Выход из режима редактирования списка
      isEditMode = false;
      saveSettings(); // Сохраняем если меняли список
      lcd.clear();
    } else {
      // Вход в редактирование текущего пункта
      enterEditMode();
    }
  }

  if (b1 == HIGH && b1Prev == LOW) {
    if (!longPressDetected) {
      // Короткое нажатие
      if (isDigitEdit) {
        nextDigit();
      } else if (isEditMode) {
        changeListValue();
      } else {
        nextMenuItem();
      }
    }
  }

  // Кнопка CHANGE (B2)
  if (b2 == LOW && b2Prev == HIGH) {
    if (isDigitEdit) {
      incrementDigit();
    } else if (isEditMode) {
      changeListValue();
    }
  }

  b1Prev = b1;
  b2Prev = b2;
}

// --- Логика Меню ---
void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 7); // 7 пунктов
  // Сброс подпункта уставки если ушли
  if (currentMenuItem != MENU_SETPOINT) spSubItem = SP_LOW;
}

void enterEditMode() {
  isEditMode = true;
  if (currentMenuItem == MENU_SETPOINT || currentMenuItem == MENU_HYST) {
    // Запуск поразрядного ввода
    if (currentMenuItem == MENU_SETPOINT) {
      tempEditValue = (spSubItem == SP_LOW) ? setpointLow : setpointHigh;
    } else {
      tempEditValue = hysteresis;
    }
    startDigitEdit(tempEditValue);
  }
  // Для RANGE, UNIT, CALIB, RESET логика обрабатывается в displayListEdit/changeListValue
}

void changeListValue() {
  if (currentMenuItem == MENU_RANGE) {
    currentRangeIndex = (currentRangeIndex + 1) % RANGES_COUNT;
  } else if (currentMenuItem == MENU_UNIT) {
    currentUnitIndex = (currentUnitIndex + 1) % UNITS_COUNT;
  } else if (currentMenuItem == MENU_SETPOINT) {
    spSubItem = (spSubItem == SP_LOW) ? SP_HIGH : SP_LOW;
  } else if (currentMenuItem == MENU_CALIB) {
    // Переключение между калибровкой Min/Max handled in display
    static bool calModeMin = true;
    calModeMin = !calModeMin;
    // В реальной реализации нужно сохранить состояние, здесь упрощено
  }
}

// --- Портазрядный ввод ---
int digits[4]; // Сотни, Десятки, Единицы, Десятые

void startDigitEdit(float val) {
  isDigitEdit = true;
  digitIndex = 0;

  // Ограничим значение максимальным рангом для корректного разбиения
  float maxVal = RANGES[currentRangeIndex];
  if (val > maxVal) val = maxVal;
  if (val < 0) val = 0;

  int v = (int)(val * 10); // Переводим в целое (например 250.5 -> 2505)

  digits[3] = v % 10; v /= 10;
  digits[2] = v % 10; v /= 10;
  digits[1] = v % 10; v /= 10;
  digits[0] = v % 10;

  // Если число маленькое, старшие разряды будут 0, это нормально
}

void nextDigit() {
  digitIndex++;
  if (digitIndex > 3) {
    // Закончили ввод
    finishDigitEdit();
  }
}

void incrementDigit() {
  digits[digitIndex]++;
  if (digits[digitIndex] > 9) digits[digitIndex] = 0;
}

float finishDigitEdit() {
  isDigitEdit = false;
  isEditMode = false;

  int v = digits[0]*1000 + digits[1]*100 + digits[2]*10 + digits[3];
  float res = (float)v / 10.0;

  float maxLimit = RANGES[currentRangeIndex];

  if (currentMenuItem == MENU_SETPOINT) {
    if (spSubItem == SP_LOW) {
      if (res >= setpointHigh) res = setpointHigh - 0.1; // Защита
      if (res < 0) res = 0;
      setpointLow = res;
    } else {
      if (res <= setpointLow) res = setpointLow + 0.1; // Защита
      if (res > maxLimit) res = maxLimit;
      setpointHigh = res;
    }
  } else if (currentMenuItem == MENU_HYST) {
    if (res < 0) res = 0;
    if (res > (maxLimit / 2)) res = maxLimit / 2;
    hysteresis = res;
  }

  saveSettings();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Saved!");
  delay(800);
  lcd.clear();
  return res;
}

// --- Отображение ---
void displayScreen() {
  float p = readPressure();

  lcd.setCursor(0, 0);
  // Показываем текущий пункт меню, если не в режиме редактирования, но можно сделать строку статуса
  // По ТЗ: 1 строка меню, 2 строка параметры.
  // В режиме просмотра: 1 строка - название пункта (или P:), 2 строка - значение

  const char* names[] = {"Mode", "Range", "Setpoint", "Hyst", "Unit", "Calibr", "Reset"};
  lcd.print(names[currentMenuItem]);
  lcd.print(":           ");

  lcd.setCursor(0, 1);
  if (currentMenuItem == MENU_MODE) {
    if (sensorError) {
      lcd.print("ERR BREAK!     ");
    } else {
      lcd.print("P:");
      lcd.print(p, 1);
      lcd.print(" ");
      lcd.print(UNIT_NAMES[currentUnitIndex]);
      lcd.print("   ");
      // Индикаторы реле
      lcd.setCursor(12, 1);
      lcd.print(valve1State?"1":"-");
      lcd.print(valve2State?"2":"-");
    }
  } else if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[currentRangeIndex], 0);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_SETPOINT) {
    lcd.print((spSubItem==SP_LOW)?"L":"H");
    lcd.print(":");
    lcd.print((spSubItem==SP_LOW)?setpointLow:setpointHigh, 1);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_HYST) {
    lcd.print("Hy:");
    lcd.print(hysteresis, 1);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[currentUnitIndex]);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_CALIB) {
    lcd.print("Press Long... ");
  } else if (currentMenuItem == MENU_RESET) {
    lcd.print("Hold to Reset ");
  }
}

void displayListEdit() {
  const char* names[] = {"Mode", "Range", "Setpoint", "Hyst", "Unit", "Calibr", "Reset"};
  lcd.setCursor(0, 0);
  lcd.print(">");
  lcd.print(names[currentMenuItem]);
  lcd.print(":           ");

  lcd.setCursor(0, 1);
  if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[currentRangeIndex], 0);
    lcd.print(" <Change>   ");
  } else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[currentUnitIndex]);
    lcd.print(" <Change>   ");
  } else if (currentMenuItem == MENU_SETPOINT) {
    lcd.print((spSubItem==SP_LOW)?" >L:":"  H:");
    lcd.print((spSubItem==SP_LOW)?setpointLow:setpointHigh, 1);
    lcd.print("        ");
  } else if (currentMenuItem == MENU_CALIB) {
    lcd.print("1.Long:Zero 2.Max");
  } else if (currentMenuItem == MENU_RESET) {
    lcd.print("Hold to Reset!");
  }
}

void displayDigitEdit() {
  lcd.setCursor(0, 0);
  if (currentMenuItem == MENU_SETPOINT) {
    lcd.print((spSubItem==SP_LOW)?"Edit L:":"Edit H:");
  } else {
    lcd.print("Edit Hy:       ");
  }

  lcd.setCursor(0, 1);
  // Рисуем цифры
  for(int i=0; i<4; i++) {
    if (i == 2) lcd.print("."); // Точка перед десятыми

    if (i == digitIndex) {
      // Мигание активного разряда
      if ((millis() / 500) % 2 == 0) {
        lcd.print(digits[i]);
      } else {
        lcd.print(" ");
      }
    } else {
      lcd.print(digits[i]);
    }
  }
  lcd.print("   ");
}

// --- Функции системы ---
float readPressure() {
  int val = analogRead(PIN_SENSOR);

  // Проверка обрыва (< 4мА ~ < 180 ADC с запасом)
  if (val < (calMinADC - 20)) {
    sensorError = true;
    return 0;
  }
  sensorError = false;

  if (val < calMinADC) val = calMinADC;
  if (val > calMaxADC) val = calMaxADC;

  float percent = (float)(val - calMinADC) / (float)(calMaxADC - calMinADC);
  float rangeVal = RANGES[currentRangeIndex];
  return percent * rangeVal;
}

void controlRelays(float p) {
  if (sensorError) {
    valve1State = false;
    valve2State = false;
  } else {
    // Логика с гистерезисом
    bool v1 = (p < (setpointLow - hysteresis));
    if (p > setpointLow) v1 = false; // Принудительное выключение при достижении

    bool v2 = (p > (setpointHigh + hysteresis));
    if (p < setpointHigh) v2 = false;

    valve1State = v1;
    valve2State = v2;
  }

  digitalWrite(PIN_RELAY_1, valve1State ? HIGH : LOW);
  digitalWrite(PIN_RELAY_2, valve2State ? HIGH : LOW);
}

// --- EEPROM ---
void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_NUM);
  EEPROM.put(ADDR_RANGE, currentRangeIndex);
  EEPROM.put(ADDR_UNIT, currentUnitIndex);
  EEPROM.put(ADDR_SP_LOW, setpointLow);
  EEPROM.put(ADDR_SP_HIGH, setpointHigh);
  EEPROM.put(ADDR_HYST, hysteresis);
  EEPROM.put(ADDR_CAL_MIN, calMinADC);
  EEPROM.put(ADDR_CAL_MAX, calMaxADC);
}

void loadSettings() {
  int magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic == MAGIC_NUM) {
    EEPROM.get(ADDR_RANGE, currentRangeIndex);
    EEPROM.get(ADDR_UNIT, currentUnitIndex);
    EEPROM.get(ADDR_SP_LOW, setpointLow);
    EEPROM.get(ADDR_SP_HIGH, setpointHigh);
    EEPROM.get(ADDR_HYST, hysteresis);
    EEPROM.get(ADDR_CAL_MIN, calMinADC);
    EEPROM.get(ADDR_CAL_MAX, calMaxADC);
  } else {
    resetSettings();
  }
}

void resetSettings() {
  currentRangeIndex = 2;
  currentUnitIndex = 1;
  setpointLow = 20.0;
  setpointHigh = 80.0;
  hysteresis = 2.0;
  calMinADC = 197;
  calMaxADC = 983;
  saveSettings();
}
