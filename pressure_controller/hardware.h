#pragma once

#include <Arduino.h>
#include <EEPROM.h>

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

constexpr unsigned long MAGIC_NUM = 12345;

// Флаг ошибки датчика (устанавливается в logic, читается везде)
extern bool sensorErrorFlag;

// Инициализация оборудования
void hardwareSetup();

// Работа с датчиком
int readSensorRaw();

// Работа с реле
void setValveState(int valveNum, bool state);
bool getValveState(int valveNum);

// Работа с EEPROM
void saveSettings();
void loadSettings();
void resetSettings();
