#include "ui.h"
#include <Wire.h> // Для инициализации шины I2C

// --- Глобальные переменные ---
// Экземпляр дисплея 16x2 по I2C-адресу 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Текущий выбранный пункт меню
MenuItems currentMenuItem = MENU_MODE;
// Флаг режима редактирования (в меню)
bool isEditMode = false;
// Флаг режима поразрядного редактирования чисел
bool isDigitEditMode = false;

// Временное значение для редактирования (не используется напрямую)
float tempEditValue = 0.0;
// Индекс текущего активного разряда (0=сотни, 1=десятки, 2=единицы, 3=десятые)
int digitIndex = 0;
// Массив цифр для поразрядного редактирования
int digits[4] = {0, 0, 0, 0};

// Время последнего нажатия кнопки (для антидребезга)
unsigned long lastBtnTime = 0;
// Время антидребезга кнопок в миллисекундах
const unsigned long DEBOUNCE = 200;
// Время для распознавания длинного нажатия в миллисекундах
const unsigned long LONG_PRESS = 1500;

// Флаг для выбора уставки (L или H) внутри пункта меню Setpoint
bool selectLowSetpoint = true;

// --- Инициализация ---
// Инициализация дисплея и вывод стартового экрана
void uiSetup() {
  lcd.init();           // Инициализация дисплея
  lcd.backlight();        // Включение подсветки
  lcd.clear();            // Очистка экрана
  lcd.print("Pressure Ctrl"); // Вывод заголовка
  lcd.setCursor(0, 1);    // Установка курсора на вторую строку
  lcd.print("Loading...");   // Вывод сообщения о загрузке
}

// --- Обработка ввода ---
// Основная функция обработки нажатий кнопок
// Реализует антидребезг и распознавание коротких/длинных нажатий
void handleButtons() {
  static bool b1Prev = HIGH, b2Prev = HIGH; // Предыдущие состояния кнопок
  static unsigned long pressStart = 0;       // Время начала нажатия
  static bool longPressed = false;           // Флаг, что длинное нажатие уже обработано

  bool b1 = digitalRead(PIN_BTN_MENU);       // Текущее состояние кнопки MENU
  bool b2 = digitalRead(PIN_BTN_CHANGE);     // Текущее состояние кнопки CHANGE
  unsigned long now = millis();              // Текущее время

  // Антидребезг: игнорируем нажатия, если прошло менее DEBOUNCE миллисекунд
  if (now - lastBtnTime < DEBOUNCE) return;

  // --- Обработка кнопки MENU ---
  // Обнаружение начала нажатия (фронт)
  if (b1 == LOW && b1Prev == HIGH) {
    pressStart = now;        // Запоминаем время начала нажатия
    longPressed = false;     // Сбрасываем флаг длинного нажатия
  }
  // Проверка на длинное нажатие (удержание)
  if (b1 == LOW && !longPressed) {
    if (now - pressStart >= LONG_PRESS) {
      longPressed = true;      // Устанавливаем флаг
      handleLongPress();       // Обрабатываем длинное нажатие
    }
  }
  // Обнаружение отпускания кнопки (спад), если длинное нажатие не было
  if (b1 == HIGH && b1Prev == LOW && !longPressed) {
    handleShortPress();      // Обрабатываем короткое нажатие
  }

  // --- Обработка кнопки CHANGE ---
  // Обнаружение нажатия (фронт)
  if (b2 == LOW && b2Prev == HIGH) {
    handleChangePress();     // Обрабатываем нажатие немедленно
  }

  // Сохраняем текущие состояния для следующего цикла
  b1Prev = b1;
  b2Prev = b2;
}

// --- Обновление вывода ---
// Центральная функция, вызываемая в loop()
// Определяет, какой экран отображать
void updateDisplay() {
  if (!isEditMode && !isDigitEditMode) {
    // Рабочий режим: читаем давление, управляем реле, выводим основной экран
    float pressure = readPressure();
    controlRelays(pressure);
    displayWorkScreen(pressure);
  } else if (isDigitEditMode) {
    // Режим поразрядного редактирования: обновляем экран с мигающим курсором
    displayDigitEditScreen();
  } else {
    // Режим обычного меню: отображаем текущий пункт
    displayMenuScreen();
  }
}

// --- Функции отображения ---
// Отображение основного рабочего экрана
// Показывает текущее давление и состояние реле
// В случае ошибки датчика выводит сообщение об ошибке
void displayWorkScreen(float pressure) {
  if (isSensorError()) {
    // Режим ошибки
    lcd.setCursor(0, 0);
    lcd.print("ERR: BREAK LINE"); // Сообщение о обрыве линии
    lcd.setCursor(0, 1);
    lcd.print("Check Sensor!   "); // Инструкция
    return;
  }

  // Режим нормальной работы
  lcd.setCursor(0, 0);
  lcd.print("P:");
  lcd.print(pressure, 1); // Текущее давление с одной цифрой после запятой
  lcd.print(" ");
  lcd.print(UNIT_NAMES[currentUnitIndex]); // Единицы измерения
  lcd.print("      "); // Пробелы для очистки остатков

  lcd.setCursor(0, 1);
  lcd.print("L:");
  lcd.print(getValveState(1) ? "ON " : "OFF"); // Состояние реле 1
  lcd.print(" H:");
  lcd.print(getValveState(2) ? "ON " : "OFF"); // Состояние реле 2
  lcd.print("    "); // Пробелы
}

// Отображение экрана меню
// Показывает название текущего пункта и его значение
void displayMenuScreen() {
  lcd.clear();
  // Массив названий пунктов меню
  const char* names[] = {"Mode", "Range", "Setpoint", "Hyst", "Unit", "Calibr", "Reset"};
  lcd.print(names[currentMenuItem]); // Выводим название пункта
  lcd.setCursor(0, 1);

  // Выводим значение или подсказку в зависимости от пункта
  if (currentMenuItem == MENU_RANGE) {
    lcd.print(RANGES[currentRangeIndex], 0); // Значение диапазона
    lcd.print(" (Change)");                 // Подсказка
  } else if (currentMenuItem == MENU_UNIT) {
    lcd.print(UNIT_NAMES[currentUnitIndex]); // Текущая единица измерения
    lcd.print(" (Change)");                 // Подсказка
  } else if (currentMenuItem == MENU_SETPOINT) {
    // Выбор между L и H
    lcd.print(selectLowSetpoint ? "Low" : "High");
    lcd.print(" Setpoint"); // Подсказка
  } else if (currentMenuItem == MENU_HYST) {
    lcd.print(hysteresis, 1); // Значение гистерезиса
    lcd.print(" (Edit)");    // Подсказка
  } else if (currentMenuItem == MENU_CALIB) {
    lcd.print("Press Enter"); // Инструкция для калибровки
  } else if (currentMenuItem == MENU_RESET) {
    lcd.print("Hold to Reset"); // Инструкция для сброса
  } else {
    lcd.print("Normal Mode"); // Для пункта Mode
  }
}

// Отображение экрана поразрядного редактирования
// Показывает редактируемое число с мигающим курсором
void displayDigitEditScreen() {
  lcd.clear();
  // Формируем метку в зависимости от редактируемого параметра
  const char* label = "";
  if (currentMenuItem == MENU_SETPOINT) {
    label = selectLowSetpoint ? "Set L:" : "Set H:";
  } else if (currentMenuItem == MENU_HYST) {
    label = "Hyst:";
  }

  lcd.print(label); // Выводим метку
  lcd.setCursor(0, 1);

  // Выводим каждую цифру
  for (int i = 0; i < 4; i++) {
    if (i == 2) lcd.print("."); // Вставляем десятичную точку
    if (i == digitIndex) {
      // Активный разряд мигает
      if ((millis() / 500) % 2 == 0) {
        lcd.print(" "); // Пробел (выключено)
      } else {
        lcd.print(digits[i]); // Цифра (включено)
      }
    } else {
      lcd.print(digits[i]); // Неактивные разряды
    }
  }
}

// --- Управление меню ---
// Переход к следующему пункту меню (по кругу)
void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 7);
}

// Вход в режим редактирования текущего пункта
void enterEditMode() {
  isEditMode = true;
  // Для числовых параметров (уставки, гистерезис) запускаем поразрядный ввод
  if (currentMenuItem == MENU_SETPOINT || currentMenuItem == MENU_HYST) {
    startDigitEdit();
  } else {
    // Для других пунктов (списки, действия)
    if (currentMenuItem == MENU_RESET) {
      resetSettings(); // Сброс настроек
      isEditMode = false;
      lcd.clear();
      lcd.print("Reset Done!");
      delay(1000);
    }
    if (currentMenuItem == MENU_CALIB) {
      // Калибровка обрабатывается отдельно
      isEditMode = false;
    }
  }
}

// Обработка длинного нажатия на кнопку MENU
// Действие зависит от текущего режима
void handleLongPress() {
  lastBtnTime = millis(); // Обновляем время последнего нажатия
  if (isDigitEditMode) {
    // В режиме редактирования: отмена без сохранения
    isDigitEditMode = false;
    isEditMode = false;
    lcd.clear();
  } else if (isEditMode) {
    // В режиме меню: выход без сохранения
    isEditMode = false;
    lcd.clear();
  } else {
    // В основном режиме: вход в редактирование
    enterEditMode();
  }
}

// Обработка короткого нажатия на кнопку MENU
// Действие зависит от текущего режима
void handleShortPress() {
  lastBtnTime = millis(); // Обновляем время последнего нажатия
  if (isDigitEditMode) {
    // В режиме поразрядного ввода: переход к следующему разряду
    nextDigit();
  } else if (isEditMode) {
    // В режиме редактирования
    if (currentMenuItem == MENU_SETPOINT) {
      // Переключение между L и H
      selectLowSetpoint = !selectLowSetpoint;
    } else {
      // Переход к следующему пункту меню
      nextMenuItem();
    }
  } else {
    // В основном режиме: переход к следующему пункту меню
    nextMenuItem();
  }
}

// Обработка нажатия на кнопку CHANGE
// Действие зависит от текущего режима
void handleChangePress() {
  lastBtnTime = millis(); // Обновляем время последнего нажатия
  if (isDigitEditMode) {
    // В режиме поразрядного ввода: увеличение цифры
    incrementDigit();
  } else if (isEditMode) {
    // В режиме редактирования
    if (currentMenuItem == MENU_RANGE) {
      // Смена диапазона
      currentRangeIndex = (currentRangeIndex + 1) % RANGES_COUNT;
      saveSettings(); // Сохраняем немедленно
    } else if (currentMenuItem == MENU_UNIT) {
      // Смена единицы измерения
      currentUnitIndex = (currentUnitIndex + 1) % UNITS_COUNT;
      saveSettings(); // Сохраняем немедленно
    }
  }
}

// --- Портазрядный ввод ---
// Инициализация сеанса поразрядного редактирования
// Подготавливает массив цифр на основе редактируемого значения
void startDigitEdit() {
  isDigitEditMode = true;
  digitIndex = 0; // Начинаем с сотен

  float val = 0;
  // Определяем, какое значение редактируется
  if (currentMenuItem == MENU_SETPOINT) {
    val = selectLowSetpoint ? setpointLow : setpointHigh;
  } else if (currentMenuItem == MENU_HYST) {
    val = hysteresis;
  }

  // Ограничиваем максимальное значение текущим диапазоном
  float maxVal = RANGES[currentRangeIndex];
  if (val > maxVal) val = maxVal;

  // Преобразуем значение в целое (умножаем на 10 для одной десятичной цифры)
  int iVal = (int)(val * 10 + 0.5);
  // Разбиваем на цифры
  digits[3] = iVal % 10;       // Десятые
  digits[2] = (iVal / 10) % 10; // Единицы
  digits[1] = (iVal / 100) % 10; // Десятки
  digits[0] = (iVal / 1000) % 10; // Сотни

  tempEditValue = val; // Сохраняем для отладки (не используется)
}

// Переход к следующему разряду при поразрядном вводе
// При переходе за последний разряд сохраняет значение
void nextDigit() {
  digitIndex++;
  if (digitIndex > 3) {
    // Все разряды отредактированы: сохраняем и выходим
    saveDigitEdit();
    isDigitEditMode = false;
    isEditMode = false;
    lcd.clear();
    lcd.print("Saved!");
    delay(800);
    lcd.clear();
  }
}

// Увеличение значения активного разряда
// При достижении 9 возвращается к 0
// Также проверяет, не превышает ли итоговое число допустимый лимит
void incrementDigit() {
  digits[digitIndex]++;
  if (digits[digitIndex] > 9) digits[digitIndex] = 0;

  // Собираем число обратно
  int total = digits[0]*1000 + digits[1]*100 + digits[2]*10 + digits[3];
  float newVal = (float)total / 10.0;

  // Определяем лимит (зависит от пункта)
  float limit = RANGES[currentRangeIndex];
  if (currentMenuItem == MENU_HYST) limit = limit /