/*
 * Pressure Controller for Arduino Nano (v4.0)
 *
 * Контроллер давления: вход 4-20 мА, LCD 1602 I2C,
 * два реле, энергонезависимые настройки.
 *
 * Безопасность: реле управляются ВСЕГДА, даже в меню настроек.
 * Обрыв датчика — аварийное отключение реле в любом режиме.
 *
 * Улучшения v4.0:
 * - Watchdog Timer (2 с) для защиты от зависаний
 * - Усреднение АЦП (16 выборок) для снижения шума
 * - Режим ADC Noise Reduction при чтении датчика
 * - Защита EEPROM от частых записей
 * - Индикация сброса от Watchdog
 *
 * Архитектура: hardware -> logic -> ui
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#include "hardware.h"
#include "logic.h"
#include "ui.h"

// ============================================================================
// Глобальные переменные
// ============================================================================

// Таймер для периодического сброса Watchdog (необязательно, т.к. wdtReset() вызывается в loop)
static unsigned long lastWdtReset = 0;
static constexpr unsigned long WDT_RESET_INTERVAL = 500;  // 500 мс

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(9600);

  // Инициализация оборудования
  hardwareSetup();

  // Загрузка настроек из EEPROM
  loadSettings();

  // Инициализация логики
  logicSetup();

  // Инициализация UI
  uiSetup();

  // Начальное состояние реле по текущему давлению
  float pressure = readPressure();
  initRelayStates(pressure);

  // Включение Watchdog Timer (2 секунды)
  wdtSetup();

  // Отображение загрузки
  delay(1000);
  uiClear();

  // Индикация сброса от Watchdog (если был)
  if (watchdogResetFlag) {
    Serial.println("WATCHDOG RESET DETECTED!");
    // Можно добавить индикацию на дисплей при первом запуске
  }
}

// ============================================================================
// Loop
// ============================================================================

void loop() {
  // 1. Сброс Watchdog Timer (каждые ~500 мс)
  unsigned long now = millis();
  if (now - lastWdtReset >= WDT_RESET_INTERVAL) {
    wdtReset();
    lastWdtReset = now;
  }

  // 2. Чтение давления с фильтрацией и управление реле
  //    Это выполняется ВСЕГДА, независимо от состояния меню
  float pressure = readPressure();
  
  // Отладка: вывод сырых значений АЦП
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 500) {
    int raw = readSensorFiltered(1);
    Serial.print("Raw ADC: ");
    Serial.print(raw);
    Serial.print(" | Voltage: ");
    Serial.print(raw * 5.0 / 1023.0, 2);
    Serial.print("V | calMin: ");
    Serial.print(calMin);
    Serial.print(" | calMax: ");
    Serial.print(calMax);
    Serial.print(" | Pressure: ");
    Serial.print(pressure);
    Serial.println();
    lastDebug = millis();
  }
  
  controlRelays(pressure);

  // 3. Обработка кнопок
  handleButtons();

  // 4. Обновление дисплея
  updateDisplay(pressure);

  // 5. Проверка необходимости сохранения настроек
  //    (только при изменении значений, защита EEPROM)
  if (shouldSaveSettings()) {
    saveSettings();
  }
}
