/*
 * Pressure Controller for Arduino Nano (v3.0)
 *
 * Контроллер давления: вход 4-20 мА, LCD 1602 I2C,
 * два реле, энергонезависимые настройки.
 *
 * Безопасность: реле управляются ВСЕГДА, даже в меню настроек.
 * Обрыв датчика — аварийное отключение реле в любом режиме.
 *
 * Архитектура: hardware -> logic -> ui
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#include "hardware.h"
#include "logic.h"
#include "ui.h"

void setup() {
  Serial.begin(9600);

  hardwareSetup();
  logicSetup();
  uiSetup();

  loadSettings();

  // Начальное состояние реле по текущему давлению
  float pressure = readPressure();
  initRelayStates(pressure);

  delay(1000);
  uiClear();
}

void loop() {
  // 1. Всегда читаем давление и управляем реле —
  //    безопасность не зависит от состояния меню
  float pressure = readPressure();
  controlRelays(pressure);

  // 2. Обработка кнопок
  handleButtons();

  // 3. Обновление дисплея
  updateDisplay(pressure);
}
