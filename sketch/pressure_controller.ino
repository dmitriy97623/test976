/*
 * Pressure Controller for Arduino Nano (v2.0 - Digit-by-Digit Input)
 *
 * Рефакторинг: основной скетч делегирует функции в модули.
 * Слои: hardware, logic, ui.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include "src/hardware.h"
#include "src/logic.h"
#include "src/ui.h"

void setup() {
  Serial.begin(9600);
  hardwareSetup();
  logicSetup();
  uiSetup();
  loadSettings();
  delay(1000);
  lcd.clear();
}

void loop() {
  handleButtons();
  updateDisplay();
}