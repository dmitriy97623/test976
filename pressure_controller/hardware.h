#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>  // Watchdog Timer
#include <avr/power.h> // Power management

// --- Пиновка ---
constexpr int PIN_SENSOR     = A0;
constexpr int PIN_RELAY_1    = 2;
constexpr int PIN_RELAY_2    = 3;
constexpr int PIN_BTN_MENU   = 4;
constexpr int PIN_BTN_CHANGE = 5;

// --- Адреса EEPROM ---
constexpr int ADDR_MAGIC     = 0;
constexpr int ADDR_RANGE     = 1;
constexpr int ADDR_UNIT      = 2;
constexpr int ADDR_SP_LOW_H  = 3;
constexpr int ADDR_SP_LOW_L  = 4;
constexpr int ADDR_SP_HIGH_H = 5;
constexpr int ADDR_SP_HIGH_L = 6;
constexpr int ADDR_HYST_H    = 7;
constexpr int ADDR_HYST_L    = 8;
constexpr int ADDR_CAL_MIN_H = 9;
constexpr int ADDR_CAL_MIN_L = 10;
constexpr int ADDR_CAL_MAX_H = 11;
constexpr int ADDR_CAL_MAX_L = 12;
constexpr int ADDR_WDT_FLAG  = 13;
constexpr int ADDR_VERSION   = 14;  // Версия структуры EEPROM  // Флаг сброса от Watchdog

constexpr unsigned long MAGIC_NUM = 12345;
constexpr byte EEPROM_VERSION = 2;  // Версия структуры EEPROM (после исправления byte)

// Флаг ошибки датчика (устанавливается в logic, читается везде)
extern bool sensorErrorFlag;

// Статус сброса (нормальный / watchdog)
extern bool watchdogResetFlag;

// Инициализация оборудования
void hardwareSetup();

// Watchdog
void wdtSetup();
void wdtReset();
void clearWdtResetFlag();

// Работа с датчиком (с усреднением и шумоподавлением)
int readSensorRaw();
int readSensorFiltered(uint8_t samples = 16);

// Работа с реле
void setValveState(int valveNum, bool state);
bool getValveState(int valveNum);

// Работа с EEPROM (с защитой от частых записей)
void saveSettings();
void loadSettings();
void resetSettings();
bool shouldSaveSettings();  // Возвращает true, если значения изменились
