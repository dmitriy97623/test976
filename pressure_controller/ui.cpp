/*
 * ui.cpp — Пользовательский интерфейс
 *
 * Машина состояний по промышленному стандарту:
 *   WORK → BROWSE → EDIT_LIST / EDIT_DIGIT / CONFIRM
 *
 * Безопасность на опасном объекте:
 *   - Таймаут возврата в рабочий режим (30 с в меню, 60 с при вводе)
 *   - Двухшаговое подтверждение сброса настроек
 *   - В меню реле управляются по уставкам (не отключаются)
 *   - Обрыв датчика — аварийный экран с полным управлением реле
 *   - Ручное управление реле ЗАПРЕЩЕНО — только автоматика
 *
 * Кнопки:
 *   MENU короткое  — навигация (следующий пункт / следующий разряд)
 *   MENU длинное   — вход / подтверждение / выход из редактирования
 *   CHANGE короткое — изменение значения (выбор из списка / +1 к цифре)
 *   CHANGE длинное  — (в EDIT_DIGIT) декремент цифры / ускоренный ввод
 */

#include "ui.h"
#include <Wire.h>

// ============================================================================
// Константы безопасности
// ============================================================================
static constexpr unsigned long TIMEOUT_BROWSE = 30000;  // 30 с — просмотр меню
static constexpr unsigned long TIMEOUT_EDIT   = 60000;  // 60 с — редактирование
static constexpr unsigned long DEBOUNCE       = 200;    // мс — антидребезг
static constexpr unsigned long LONG_PRESS     = 1500;   // мс — порог длинного нажатия
static constexpr unsigned long BLINK_PERIOD   = 250;    // мс — период мигания курсора
static constexpr unsigned long MSG_DISPLAY_TIME = 1500;  // мс — время показа сообщений

// ============================================================================
// Дисплей
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================================
// Состояние UI
// ============================================================================
static UiState  uiState        = STATE_WORK;
static MenuItems currentMenu   = MENU_RANGE;
static unsigned long msgShowTime = 0;  // Время начала показа сообщения
static unsigned long lastActivity = 0;  // Время последнего действия пользователя

// --- Поразрядный ввод ---
static int  digitIndex = 0;
static int  digits[4]  = {0, 0, 0, 0};

// --- Экраны рабочего режима ---
enum WorkScreen { WORK_MAIN, WORK_LO, WORK_HI };
static WorkScreen workScreen = WORK_MAIN;
static unsigned long workScreenTime = 0;
static constexpr unsigned long WORK_SCREEN_HOLD = 3000;  // Показ уставок 3 с

// --- Кэш дисплея ---
static char cacheLine0[17] = "";
static char cacheLine1[17] = "";
static unsigned long lastDisplayUpdate = 0;
static constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 400;  // мс — мин. интервал обновления

// --- Внутренний флаг калибровки ---
static bool calibratingFlag = false;

// ============================================================================
// Вспомогательные функции дисплея
// ============================================================================

static void writeLine(int row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t len = strlen(text);
  lcd.print(text);
  for (uint8_t i = len; i < 16; i++) {
    lcd.print(' ');
  }
}

static void updateLine(int row, const char* newText) {
  const char* cached = (row == 0) ? cacheLine0 : cacheLine1;
  if (strcmp(cached, newText) == 0) return;
  strcpy(const_cast<char*>(cached), newText);
  writeLine(row, newText);
}

static void invalidateCache() {
  strcpy(cacheLine0, "");
  strcpy(cacheLine1, "");
}

// ============================================================================
// Инициализация
// ============================================================================

void uiSetup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  invalidateCache();
  writeLine(0, "Pressure Ctrl");
  writeLine(1, "Loading...");
}

void uiClear() {
  lcd.clear();
  invalidateCache();
}

// ============================================================================
// Запросы состояния
// ============================================================================

bool isInMenu() {
  return uiState != STATE_WORK;
}

bool isCalibrating() {
  return calibratingFlag;
}

// ============================================================================
// Безопасность: таймаут неактивности → возврат в рабочий режим
// ============================================================================

static void touchActivity() {
  lastActivity = millis();
}

static void checkTimeout() {
  if (uiState == STATE_WORK) return;
  unsigned long timeout = (uiState == STATE_BROWSE) ? TIMEOUT_BROWSE : TIMEOUT_EDIT;
  if (millis() - lastActivity >= timeout) {
    uiState = STATE_WORK;
    workScreen = WORK_MAIN;
    invalidateCache();
  }
}

// ============================================================================
// Переходы между состояниями
// ============================================================================

static void goToWork() {
  uiState = STATE_WORK;
  workScreen = WORK_MAIN;
  invalidateCache();
}

static void goToBrowse() {
  uiState = STATE_BROWSE;
  invalidateCache();
  touchActivity();
}

static void goToEditList() {
  uiState = STATE_EDIT_LIST;
  invalidateCache();
  touchActivity();
}

static void goToEditDigit() {
  uiState = STATE_EDIT_DIGIT;
  invalidateCache();
  touchActivity();
}

static void goToConfirm() {
  uiState = STATE_CONFIRM;
  invalidateCache();
  touchActivity();
}

// ============================================================================
// Поразрядный ввод
// ============================================================================

static void startDigitEdit(float val) {
  digitIndex = 0;

  float maxVal = RANGES[currentRangeIndex];
  if (val > maxVal) val = maxVal;
  if (val < 0.0f)   val = 0.0f;

  int iVal = (int)(val * 10 + 0.5f);
  digits[3] = iVal % 10;
  digits[2] = (iVal / 10) % 10;
  digits[1] = (iVal / 100) % 10;
  digits[0] = (iVal / 1000) % 10;
}

static float digitsToValue() {
  int total = digits[0] * 1000 + digits[1] * 100 + digits[2] * 10 + digits[3];
  return (float)total / 10.0f;
}

static void incrementDigit() {
  digits[digitIndex]++;
  if (digits[digitIndex] > 9) digits[digitIndex] = 0;

  float newVal = digitsToValue();
  float limit  = RANGES[currentRangeIndex];
  if (currentMenu == MENU_HYST) limit = limit / 2.0f;

  if (newVal > limit) {
    for (int i = 0; i < 4; i++) digits[i] = 0;
  }
}

static void decrementDigit() {
  digits[digitIndex]--;
  if (digits[digitIndex] < 0) digits[digitIndex] = 9;

  float newVal = digitsToValue();
  if (newVal < 0.0f) {
    for (int i = 0; i < 4; i++) digits[i] = 0;
  }
}

static bool saveDigitEdit() {
  float newVal = digitsToValue();
  if (newVal < 0.0f) newVal = 0.0f;

  switch (currentMenu) {
    case MENU_SP_LOW:
      setpointLow = newVal;
      if (setpointLow >= setpointHigh) {
        setpointHigh = setpointLow + 1.0f;
        float maxVal = RANGES[currentRangeIndex];
        if (setpointHigh > maxVal) {
          setpointHigh = maxVal;
          setpointLow  = maxVal - 1.0f;
        }
      }
      break;

    case MENU_SP_HIGH:
      setpointHigh = newVal;
      if (setpointHigh <= setpointLow) {
        setpointLow = setpointHigh - 1.0f;
        if (setpointLow < 0.0f) {
          setpointLow  = 0.0f;
          setpointHigh = 1.0f;
        }
      }
      break;

    case MENU_HYST:
      hysteresis = newVal;
      break;

    default:
      return false;
  }

  saveSettings();
  return true;
}

// ============================================================================
// Обработка кнопок
// ============================================================================

static void onMenuShort();
static void onMenuLong();
static void onChangeShort();
static void onChangeLong();

void handleButtons() {
  static bool b1Prev = HIGH, b2Prev = HIGH;
  static unsigned long pressStart1 = 0, pressStart2 = 0;
  static bool longPressed1 = false, longPressed2 = false;

  bool b1 = digitalRead(PIN_BTN_MENU);
  bool b2 = digitalRead(PIN_BTN_CHANGE);
  unsigned long now = millis();

  // --- Кнопка MENU ---
  if (b1 == LOW && b1Prev == HIGH) {
    pressStart1 = now;
    longPressed1 = false;
  }
  if (b1 == LOW && !longPressed1 && (now - pressStart1 >= LONG_PRESS)) {
    longPressed1 = true;
    onMenuLong();
  }
  if (b1 == HIGH && b1Prev == LOW && !longPressed1) {
    if (now - lastActivity >= DEBOUNCE) {
      onMenuShort();
    }
  }

  // --- Кнопка CHANGE ---
  if (b2 == LOW && b2Prev == HIGH) {
    pressStart2 = now;
    longPressed2 = false;
  }
  if (b2 == LOW && !longPressed2 && (now - pressStart2 >= LONG_PRESS)) {
    longPressed2 = true;
    onChangeLong();
  }
  if (b2 == HIGH && b2Prev == LOW && !longPressed2) {
    if (now - lastActivity >= DEBOUNCE) {
      onChangeShort();
    }
  }

  b1Prev = b1;
  b2Prev = b2;
}

// ============================================================================
// Обработчики: MENU короткое — навигация
// ============================================================================

static void onMenuShort() {
  touchActivity();

  switch (uiState) {
    case STATE_WORK:
      // На рабочем экране — ничего (навигация только в меню)
      break;

    case STATE_BROWSE:
      // Следующий пункт меню
      currentMenu = (MenuItems)((currentMenu + 1) % MENU_COUNT);
      invalidateCache();
      break;

    case STATE_EDIT_LIST:
      // Подтвердить выбор → вернуться в просмотр
      saveSettings();
      goToBrowse();
      break;

    case STATE_EDIT_DIGIT:
      // Следующий разряд; после последнего — подтвердить
      digitIndex++;
      if (digitIndex > 3) {
        saveDigitEdit();
        goToBrowse();
      }
      invalidateCache();
      break;

    case STATE_CONFIRM:
      // Короткое MENU в подтверждении — отмена (безопасность!)
      goToBrowse();
      break;
  }
}

// ============================================================================
// Обработчики: MENU длинное — вход / подтверждение / выход
// ============================================================================

static void onMenuLong() {
  touchActivity();

  switch (uiState) {
    case STATE_WORK:
      // Вход в меню настроек
      currentMenu = MENU_RANGE;
      goToBrowse();
      break;

    case STATE_BROWSE:
      // Выход из меню в рабочий режим
      goToWork();
      break;

    case STATE_EDIT_LIST:
      // Отмена → вернуться в просмотр без сохранения
      // (загружаем сохранённые значения)
      loadSettings();
      goToBrowse();
      break;

    case STATE_EDIT_DIGIT:
      // Отмена без сохранения → вернуться в просмотр
      loadSettings();
      goToBrowse();
      break;

    case STATE_CONFIRM:
      // Подтвердить опасное действие
      if (currentMenu == MENU_RESET) {
        resetSettings();
        goToWork();
        // Показать подтверждение без блокировки
        invalidateCache();
        writeLine(0, "Reset Done!    ");
        writeLine(1, "               ");
        msgShowTime = millis();
      }
      // MENU_CALIB: подтверждение калибровки (заглушка)
      break;
  }
}

// ============================================================================
// Обработчики: CHANGE короткое — изменение значения
// ============================================================================

static void onChangeShort() {
  touchActivity();

  switch (uiState) {
    case STATE_WORK:
      // На рабочем экране: показать уставки (информационно)
      if (workScreen == WORK_MAIN) {
        workScreen = WORK_LO;
        workScreenTime = millis();
      } else if (workScreen == WORK_LO) {
        workScreen = WORK_HI;
        workScreenTime = millis();
      } else {
        workScreen = WORK_MAIN;
      }
      invalidateCache();
      break;

    case STATE_BROWSE:
      // Войти в редактирование текущего пункта
      switch (currentMenu) {
        case MENU_RANGE:
        case MENU_UNIT:
          goToEditList();
          break;
        case MENU_SP_LOW:
          startDigitEdit(setpointLow);
          goToEditDigit();
          break;
        case MENU_SP_HIGH:
          startDigitEdit(setpointHigh);
          goToEditDigit();
          break;
        case MENU_HYST:
          startDigitEdit(hysteresis);
          goToEditDigit();
          break;
        case MENU_RESET:
        case MENU_CALIB:
          goToConfirm();
          break;
      }
      break;

    case STATE_EDIT_LIST:
      // Циклический перебор вариантов
      if (currentMenu == MENU_RANGE) {
        currentRangeIndex = (currentRangeIndex + 1) % RANGES_COUNT;
        invalidateCache();
      } else if (currentMenu == MENU_UNIT) {
        currentUnitIndex = (currentUnitIndex + 1) % UNITS_COUNT;
        invalidateCache();
      }
      break;

    case STATE_EDIT_DIGIT:
      // +1 к текущей цифре
      incrementDigit();
      invalidateCache();
      break;

    case STATE_CONFIRM:
      // В режиме подтверждения CHANGE — отмена (безопасность!)
      goToBrowse();
      break;
  }
}

// ============================================================================
// Обработчики: CHANGE длинное — декремент / спецфункция
// ============================================================================

static void onChangeLong() {
  touchActivity();

  switch (uiState) {
    case STATE_EDIT_DIGIT:
      // Декремент текущей цифры (ускоренный ввод)
      decrementDigit();
      invalidateCache();
      break;

    default:
      // В остальных состояниях — ничего
      break;
  }
}

// ============================================================================
// Экранные функции
// ============================================================================

static void displayWorkMain(float pressure) {
  char l0[17], l1[17];

  if (sensorErrorFlag) {
    snprintf(l0, sizeof(l0), "ERR: BREAK LINE");
    snprintf(l1, sizeof(l1), "Check Sensor!  ");
  } else if (watchdogResetFlag) {
    snprintf(l0, sizeof(l0), "WDT RESET!      ");
    char bufP[10]; dtostrf(pressure, 5, 1, bufP); snprintf(l1, sizeof(l1), "P:%-6s%s    ", bufP, UNIT_NAMES[currentUnitIndex]);
  } else {
    char bufP2[10]; dtostrf(pressure, 5, 1, bufP2); snprintf(l0, sizeof(l0), "P:%-6s%s    ", bufP2, UNIT_NAMES[currentUnitIndex]);
    snprintf(l1, sizeof(l1), "L:%s H:%s    ",
             getValveState(1) ? "ON " : "OFF",
             getValveState(2) ? "ON " : "OFF");
  }

  updateLine(0, l0);
  updateLine(1, l1);
}

static void displayWorkSetpoints() {
  char l0[17], l1[17];

  char bufLo[8]; dtostrf(setpointLow, 5, 1, bufLo); snprintf(l0, sizeof(l0), "Lo:%-6s%s   ", bufLo, UNIT_NAMES[currentUnitIndex]);
  char bufHi[8]; dtostrf(setpointHigh, 5, 1, bufHi); snprintf(l1, sizeof(l1), "Hi:%-6s%s   ", bufHi, UNIT_NAMES[currentUnitIndex]);

  updateLine(0, l0);
  updateLine(1, l1);
}

static void displayBrowse() {
  static const char* const names[] = {
    "1.Range", "2.SpLow", "3.SpHigh",
    "4.Hyst",  "5.Unit",  "6.Calibr", "7.Reset"
  };

  char l0[17], l1[17];

  snprintf(l0, sizeof(l0), "> %s", names[currentMenu]);

  switch (currentMenu) {
    case MENU_RANGE:
      snprintf(l1, sizeof(l1), "  %d %s       ", (int)RANGES[currentRangeIndex], UNIT_NAMES[currentUnitIndex]);
      break;
    case MENU_SP_LOW:
      char buf[6]; dtostrf(setpointLow, 4, 1, buf); snprintf(l1, sizeof(l1), "  %s %s      ", buf, UNIT_NAMES[currentUnitIndex]);
      break;
    case MENU_SP_HIGH:
      char buf2[6]; dtostrf(setpointHigh, 4, 1, buf2); snprintf(l1, sizeof(l1), "  %s %s      ", buf2, UNIT_NAMES[currentUnitIndex]);
      break;
    case MENU_HYST:
      char buf3[6]; dtostrf(hysteresis, 4, 1, buf3); snprintf(l1, sizeof(l1), "  %s %s      ", buf3, UNIT_NAMES[currentUnitIndex]);
      break;
    case MENU_UNIT:
      snprintf(l1, sizeof(l1), "  %s          ", UNIT_NAMES[currentUnitIndex]);
      break;
    case MENU_CALIB:
      snprintf(l1, sizeof(l1), "  [CHANGE]=go  ");
      break;
    case MENU_RESET:
      snprintf(l1, sizeof(l1), "  [CHANGE]=go  ");
      break;
    default:
      snprintf(l1, sizeof(l1), "                ");
      break;
  }

  updateLine(0, l0);
  updateLine(1, l1);
}

static void displayEditList() {
  char l0[17], l1[17];

  if (currentMenu == MENU_RANGE) {
    snprintf(l0, sizeof(l0), "Range:         ");
    snprintf(l1, sizeof(l1), "  %d %s   << >>", (int)RANGES[currentRangeIndex], UNIT_NAMES[currentUnitIndex]);
  } else if (currentMenu == MENU_UNIT) {
    snprintf(l0, sizeof(l0), "Unit:          ");
    snprintf(l1, sizeof(l1), "  %s       << >>", UNIT_NAMES[currentUnitIndex]);
  }

  updateLine(0, l0);
  updateLine(1, l1);
}

static void displayEditDigit() {
  char l0[17], l1[17];

  const char* label = "";
  switch (currentMenu) {
    case MENU_SP_LOW:  label = "SpLow:";  break;
    case MENU_SP_HIGH: label = "SpHigh:"; break;
    case MENU_HYST:    label = "Hyst:";   break;
    default:           label = "Edit:";   break;
  }

  snprintf(l0, sizeof(l0), "%s           ", label);

  // Мигающий курсор: пробел/цифра
  bool blinkOn = (millis() / BLINK_PERIOD) % 2 == 0;

  char num[6];
  int pos = 0;
  for (int i = 0; i < 4; i++) {
    if (i == 3) num[pos++] = '.';
    if (i == digitIndex) {
      num[pos++] = blinkOn ? ' ' : ('0' + digits[i]);
    } else {
      num[pos++] = '0' + digits[i];
    }
  }
  num[pos] = '\0';

  snprintf(l1, sizeof(l1), " %s  <> ^v     ", num);

  updateLine(0, l0);
  updateLine(1, l1);
}

static void displayConfirm() {
  char l0[17], l1[17];

  if (currentMenu == MENU_RESET) {
    snprintf(l0, sizeof(l0), "!! RESET ALL !!");
    snprintf(l1, sizeof(l1), "LONG=Yes SHRT=No");
  } else if (currentMenu == MENU_CALIB) {
    snprintf(l0, sizeof(l0), "!! CALIBRATE !!");
    snprintf(l1, sizeof(l1), "LONG=Yes SHRT=No");
  }

  updateLine(0, l0);
  updateLine(1, l1);
}

// ============================================================================
// Обновление дисплея (главная функция)
// ============================================================================

void updateDisplay(float pressure) {
  // Ограничение частоты обновления дисплея (300 мс)
  unsigned long now = millis();
  if (now - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) return;
  lastDisplayUpdate = now;

  // Автоматическое скрытие сообщения после сброса
  if (msgShowTime > 0 && (millis() - msgShowTime >= MSG_DISPLAY_TIME)) {
    msgShowTime = 0;
    invalidateCache();
  }
  checkTimeout();

  // Обрыв датчика — аварийный экран ВНЕ ЗАВИСИМОСТИ от состояния меню
  // (кроме редактирования — чтобы не потерять ввод)
  if (sensorErrorFlag && uiState != STATE_EDIT_DIGIT && uiState != STATE_EDIT_LIST) {
    char l0[17], l1[17];
    snprintf(l0, sizeof(l0), "ERR: BREAK LINE");
    snprintf(l1, sizeof(l1), "L:OFF H:OFF    ");
    updateLine(0, l0);
    updateLine(1, l1);
    return;
  }

  switch (uiState) {
    case STATE_WORK: {
      // Автовозврат из информационных экранов уставок
      if (workScreen != WORK_MAIN && (millis() - workScreenTime >= WORK_SCREEN_HOLD)) {
        workScreen = WORK_MAIN;
        invalidateCache();
      }

      if (workScreen == WORK_MAIN) {
        displayWorkMain(pressure);
      } else {
        displayWorkSetpoints();
      }
      break;
    }

    case STATE_BROWSE:
      displayBrowse();
      break;

    case STATE_EDIT_LIST:
      displayEditList();
      break;

    case STATE_EDIT_DIGIT:
      displayEditDigit();
      break;

    case STATE_CONFIRM:
      displayConfirm();
      break;
  }
}
