#include "ui.h"
#include <Wire.h>

// --- Глобальные переменные ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

MenuItems currentMenuItem = MENU_MODE;
bool isEditMode = false;
bool isDigitEditMode = false;

float tempEditValue = 0.0;
int digitIndex = 0;
int digits[4] = {0, 0, 0, 0};

unsigned long lastBtnTime = 0;
const unsigned long DEBOUNCE = 200;
const unsigned long LONG_PRESS = 1500;

bool selectLowSetpoint = true; // Для выбора L/H в меню

void uiSetup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Pressure Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
}

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

void updateDisplay() {
  if (!isEditMode && !isDigitEditMode) {
    float pressure = readPressure();
    controlRelays(pressure);
    displayWorkScreen(pressure);
  } else if (isDigitEditMode) {
    displayDigitEditScreen();
  } else {
    displayMenuScreen();
  }
}

void displayWorkScreen(float pressure) {
  if (isSensorError()) {
    lcd.setCursor(0, 0);
    lcd.print("ERR: BREAK LINE");
    lcd.setCursor(0, 1);
    lcd.print("Check Sensor!   ");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("P:");
  lcd.print(pressure, 1);
  lcd.print(" ");
  lcd.print(UNIT_NAMES[currentUnitIndex]);
  lcd.print("      ");

  lcd.setCursor(0, 1);
  lcd.print("L:");
  lcd.print(getValveState(1) ? "ON " : "OFF");
  lcd.print(" H:");
  lcd.print(getValveState(2) ? "ON " : "OFF");
  lcd.print("    ");
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

void nextMenuItem() {
  currentMenuItem = (MenuItems)((currentMenuItem + 1) % 7);
}

void enterEditMode() {
  isEditMode = true;
  if (currentMenuItem == MENU_SETPOINT || currentMenuItem == MENU_HYST) {
    startDigitEdit();
  } else {
    if (currentMenuItem == MENU_RESET) {
      resetSettings();
      isEditMode = false;
      lcd.clear();
      lcd.print("Reset Done!");
      delay(1000);
    }
    if (currentMenuItem == MENU_CALIB) {
      // Калибровка — отдельная функция
      isEditMode = false;
    }
  }
}

void handleLongPress() {
  lastBtnTime = millis();
  if (isDigitEditMode) {
    isDigitEditMode = false;
    isEditMode = false;
    lcd.clear();
  } else if (isEditMode) {
    isEditMode = false;
    lcd.clear();
  } else {
    enterEditMode();
  }
}

void handleShortPress() {
  lastBtnTime = millis();
  if (isDigitEditMode) {
    nextDigit();
  } else if (isEditMode) {
    if (currentMenuItem == MENU_SETPOINT) {
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
    if (currentMenuItem == MENU_RANGE) {
      currentRangeIndex = (currentRangeIndex + 1) % RANGES_COUNT;
      saveSettings();
    } else if (currentMenuItem == MENU_UNIT) {
      currentUnitIndex = (currentUnitIndex + 1) % UNITS_COUNT;
      saveSettings();
    }
  }
}

void startDigitEdit() {
  isDigitEditMode = true;
  digitIndex = 0;

  float val = 0;
  if (currentMenuItem == MENU_SETPOINT) {
    val = selectLowSetpoint ? setpointLow : setpointHigh;
  } else if (currentMenuItem == MENU_HYST) {
    val = hysteresis;
  }

  float maxVal = RANGES[currentRangeIndex];
  if (val > maxVal) val = maxVal;

  int iVal = (int)(val * 10 + 0.5);
  digits[3] = iVal % 10;
  digits[2] = (iVal / 10) % 10;
  digits[1] = (iVal / 100) % 10;
  digits[0] = (iVal / 1000) % 10;

  tempEditValue = val;
}

void nextDigit() {
  digitIndex++;
  if (digitIndex > 3) {
    saveDigitEdit();
    isDigitEditMode = false;
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

  int total = digits[0]*1000 + digits[1]*100 + digits[2]*10 + digits[3];
  float newVal = (float)total / 10.0;

  float limit = RANGES[currentRangeIndex];
  if (currentMenuItem == MENU_HYST) limit = limit / 2.0;

  if (newVal > limit) {
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