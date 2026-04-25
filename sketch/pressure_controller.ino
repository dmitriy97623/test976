/*
 * Pressure Controller for Arduino Nano
 * 
 * Функционал:
 * - Чтение датчика 4-20мА (через резистор 240 Ом на A0)
 * - Управление 2 реле (D2, D3) по уставкам с гистерезисом
 * - Дисплей 1602 I2C (A4-SDA, A5-SCL)
 * - Меню настройки: Режим, Ранг, Уставка, Ед. Изм.
 * - Навигация 2 кнопками (D4 - Menu/Enter, D5 - Change)
 * 
 * Поддерживаемые диапазоны (Ранги): 1, 40, 250, 400, 600, 1000
 * Поддерживаемые единицы: Па, кПа, МПа, бар, мбар
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Конфигурация пинов ---
const int PIN_SENSOR = A0;
const int PIN_RELAY_1 = 2;
const int PIN_RELAY_2 = 3;
const int PIN_BTN_MENU = 4; // Кнопка 1: Переключение / Вход / Выход
const int PIN_BTN_CHANGE = 5; // Кнопка 2: Изменение значения

// --- Настройки дисплея ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Калибровка АЦП (подобрать под конкретный резистор и источник опорного напряжения) ---
// При 5В питании и 10 битах: 4мА (0.96В) ~ 197, 20мА (4.8В) ~ 983
const int ADC_MIN = 197; 
const int ADC_MAX = 983;

// --- Константы диапазонов (Ранги) ---
const int RANGES_COUNT = 6;
const float RANGES[RANGES_COUNT] = {1.0, 40.0, 250.0, 400.0, 600.0, 1000.0};

// --- Константы единиц измерения ---
enum UnitType { UNIT_PA, UNIT_KPA, UNIT_MPA, UNIT_BAR, UNIT_MBAR };
const char* UNIT_NAMES[] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const int UNITS_COUNT = 5;

// Коэффициенты пересчета от базового диапазона (условно считаем входной сигнал % от диапазона, а потом масштабируем)
// Логика: Датчик измеряет 0..Range. Мы переводим это в выбранные единицы.
// Для упрощения: считаем, что датчик откалиброван на выбранный Range.
// Значение = (Процент / 100) * Range * КоэффициентЕдиницы

// --- Глобальные переменные состояния ---
int currentRangeIndex = 2; // По умолчанию 250
int currentUnitIndex = 1;  // По умолчанию кПа
float setpointLow = 20.0;  // Нижняя уставка (в текущих ед. изм.)
float setpointHigh = 80.0; // Верхняя уставка (в текущих ед. изм.)
float hysteresis = 2.0;    // Гистерезис

bool valve1State = false;
bool valve2State = false;

// --- Переменные меню ---
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_UNIT };
MenuItems currentMenuItem = MENU_MODE;
bool isEditMode = false;
unsigned long lastBtnPressTime = 0;
const unsigned long LONG_PRESS_TIME = 2000; // 2 секунды для входа в режим редактирования
const unsigned long DEBOUNCE_DELAY = 300;   // Антидребезг

// Временные переменные для редактирования
int tempRangeIndex = 0;
int tempUnitIndex = 0;
float tempSetpointLow = 0;
float tempSetpointHigh = 0;

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
  lcd.setCursor(0, 0);
  lcd.print("Pressure Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
  delay(1500);
  lcd.clear();
  
  updateRelays(0); // Инициализация реле
}

void loop() {
  static unsigned long lastLoopTime = 0;
  if (millis() - lastLoopTime < 100) return; // Опрос раз в 100мс
  lastLoopTime = millis();

  handleButtons();
  
  if (!isEditMode) {
    // Рабочий режим: чтение датчика и управление
    float pressureValue = readPressure();
    controlRelays(pressureValue);
    displayWorkScreen(pressureValue);
  } else {
    // Режим редактирования
    displayEditScreen();
  }
}

// --- Чтение датчика ---
float readPressure() {
  int sensorValue = analogRead(PIN_SENSOR);
  // Ограничение значений
  if (sensorValue < ADC_MIN) sensorValue = ADC_MIN;
  if (sensorValue > ADC_MAX) sensorValue = ADC_MAX;

  // Получаем процент от диапазона (0.0 ... 100.0)
  float percent = (float)(sensorValue - ADC_MIN) / (float)(ADC_MAX - ADC_MIN) * 100.0;
  
  // Переводим в физические единицы
  float rangeVal = RANGES[currentRangeIndex];
  float value = (percent / 100.0) * rangeVal;
  
  // Применяем коэффициент единицы измерения
  // Базово считаем, что Range задан в тех единицах, которые выбраны по умолчанию для этого ранга? 
  // Нет, пользователь выбирает единицы независимо. 
  // Допустим, Ранг 250 - это 250 бар (или кПа, в зависимости от контекста датчика).
  // Но в ТЗ сказано: ранги 0-1, 40... и единицы мбар, бар...
  // Предположим, что числовое значение ранга соответствует выбранной единице измерения напрямую.
  // Т.е. если Ранг 250 и Ед. Бар -> макс давление 250 бар.
  // Если Ранг 250 и Ед. кПа -> макс давление 250 кПа.
  
  return value;
}

// --- Управление реле ---
void controlRelays(float pressure) {
  bool v1 = false;
  bool v2 = false;

  // Логика с гистерезисом
  // Клапан 1 (Например, на наполнение/сброс): Открывается если давление НИЖЕ низкого порога
  if (pressure < (setpointLow - hysteresis)) v1 = true;
  if (pressure > setpointLow) v1 = false;

  // Клапан 2 (Например, на сброс/наполнение): Открывается если давление ВЫШЕ высокого порога
  if (pressure > (setpointHigh + hysteresis)) v2 = true;
  if (pressure < setpointHigh) v2 = false;
  
  // Блокировка одновременного включения (опционально, зависит от логики системы)
  // if (v1 && v2) { v1 = false; v2 = false; } 

  updateRelays(v1, v2);
}

void updateRelays(bool v1, bool v2) {
  valve1State = v1;
  valve2State = v2;
  digitalWrite(PIN_RELAY_1, v1 ? HIGH : LOW);
  digitalWrite(PIN_RELAY_2, v2 ? HIGH : LOW);
}

void updateRelays(int state) { // Для инициализации
  digitalWrite(PIN_RELAY_1, LOW);
  digitalWrite(PIN_RELAY_2, LOW);
}

// --- Обработка кнопок ---
void handleButtons() {
  static bool btn1Prev = HIGH;
  static bool btn2Prev = HIGH;
  static unsigned long btn1PressStart = 0;
  static bool longPressDetected = false;

  bool btn1 = digitalRead(PIN_BTN_MENU);
  bool btn2 = digitalRead(PIN_BTN_CHANGE);

  // Кнопка 1 (Menu/Enter)
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
        // В режиме редактирования короткое нажатие может переключать поле внутри пункта или подтверждать?
        // Сделаем так: длинное - вход/выход, короткое в режиме редактирования - переход между полями (если их несколько) или инкремент?
        // По ТЗ: "Кнопка 1 шагать по меню", "Кнопка 2 менять параметры".
        // Значит в режиме редактирования Кнопка 1 должна подтверждать и выходить? Или переключать между Lower/Upper Setpoint?
        // Реализуем: В режиме редактирования Кнопка 1 переключает фокус (если нужно) или просто игнорируется, а выход только по длинному?
        // Давайте сделаем проще: Длинное - Вход/Выход с сохранением. Короткое в меню - выбор пункта. Короткое в редакторе - переключение между уставками (Low/High).
        cycleSetpointFocus();
      }
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
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 4);
  // Сброс фокуса уставки при выходе из пункта
  if (currentMenuItem != MENU_SETPOINT) {
    setpointFocusLow = true; 
  }
  delay(DEBOUNCE_DELAY);
}

bool setpointFocusLow = true; // true - нижняя, false - верхняя

void cycleSetpointFocus() {
  if (currentMenuItem == MENU_SETPOINT) {
    setpointFocusLow = !setpointFocusLow;
  }
  delay(DEBOUNCE_DELAY);
}

void toggleEditMode() {
  isEditMode = !isEditMode;
  if (isEditMode) {
    // Сохраняем текущие значения во временные
    tempRangeIndex = currentRangeIndex;
    tempUnitIndex = currentUnitIndex;
    tempSetpointLow = setpointLow;
    tempSetpointHigh = setpointHigh;
    setpointFocusLow = true;
  } else {
    // Применяем изменения
    currentRangeIndex = tempRangeIndex;
    currentUnitIndex = tempUnitIndex;
    setpointLow = tempSetpointLow;
    setpointHigh = tempSetpointHigh;
    
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
        tempSetpointLow += 1.0; // Шаг 1 единица
        if (tempSetpointLow > tempSetpointHigh) tempSetpointLow = tempSetpointHigh;
      } else {
        tempSetpointHigh += 1.0;
        if (tempSetpointHigh > RANGES[tempRangeIndex]) tempSetpointHigh = RANGES[tempRangeIndex];
      }
      break;
    case MENU_MODE:
      // Режим пока без действий, можно добавить позже
      break;
  }
  delay(DEBOUNCE_DELAY);
}

// --- Экраны ---
void displayWorkScreen(float pressure) {
  lcd.setCursor(0, 0);
  lcd.print("P: ");
  lcd.print(pressure, 1);
  lcd.print(" ");
  lcd.print(UNIT_NAMES[currentUnitIndex]);
  lcd.print("       "); // Очистка хвоста
  
  lcd.setCursor(0, 1);
  lcd.print("V1:");
  lcd.print(valve1State ? "ON " : "OFF");
  lcd.print(" V2:");
  lcd.print(valve2State ? "ON " : "OFF");
  lcd.print("       ");
}

void displayEditScreen() {
  lcd.setCursor(0, 0);
  switch (currentMenuItem) {
    case MENU_MODE:
      lcd.print("Mode: Normal    ");
      lcd.setCursor(0, 1);
      lcd.print("[Edit Mode]     ");
      break;
    case MENU_RANGE:
      lcd.print("Range:          ");
      lcd.setCursor(0, 1);
      lcd.print(RANGES[tempRangeIndex], 0);
      lcd.print("        ");
      break;
    case MENU_SETPOINT:
      lcd.print("Setpoint:       ");
      lcd.setCursor(0, 1);
      if (setpointFocusLow) {
        lcd.print(">L:");
        lcd.print(tempSetpointLow, 1);
        lcd.print("        ");
      } else {
        lcd.print(" H:");
        lcd.print(tempSetpointHigh, 1);
        lcd.print("        ");
      }
      break;
    case MENU_UNIT:
      lcd.print("Unit:           ");
      lcd.setCursor(0, 1);
      lcd.print(UNIT_NAMES[tempUnitIndex]);
      lcd.print("        ");
      break;
  }
  
  // Индикатор текущего пункта меню в верхней строке (можно подсветить или добавить маркер)
  // В данной реализации просто пишем название
  lcd.setCursor(0, 0);
  const char* menuNames[] = {"Mode", "Range", "Setpoint", "Unit"};
  lcd.print(menuNames[currentMenuItem]);
  lcd.print(":             ");
  
  // Перерисовка значения снизу с учетом позиции курсора
  lcd.setCursor(0, 1);
  if (currentMenuItem == MENU_RANGE) {
      lcd.print(RANGES[tempRangeIndex], 0);
      lcd.print("        ");
  } else if (currentMenuItem == MENU_UNIT) {
      lcd.print(UNIT_NAMES[tempUnitIndex]);
      lcd.print("        ");
  } else if (currentMenuItem == MENU_SETPOINT) {
      if (setpointFocusLow) {
        lcd.print(">L:");
        lcd.print(tempSetpointLow, 1);
        lcd.print("        ");
      } else {
        lcd.print(" H:");
        lcd.print(tempSetpointHigh, 1);
        lcd.print("        ");
      }
  } else {
      lcd.print("Normal Mode     ");
  }
}
