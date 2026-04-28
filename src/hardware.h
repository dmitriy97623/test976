#pragma once

#include <Arduino.h>
#include <EEPROM.h>

// --- Пиновка ---
extern const int PIN_SENSOR;
extern const int PIN_RELAY_1;
extern const int PIN_RELAY_2;
extern const int PIN_BTN_MENU;
extern const int PIN_BTN_CHANGE;

// --- EEPROM ---
// Адреса хранения настроек
extern const int ADDR_MAGIC;
extern const int ADDR_RANGE;
extern const int ADDR_UNIT;
extern const int ADDR_SP_LOW_H;
extern const int ADDR_SP_LOW_L;
extern const int ADDR_SP_HIGH_H;
extern const int ADDR_SP_HIGH_L;
extern const int ADDR_HYST_H;
extern const int ADDR_HYST_L;
extern const int ADDR_CAL_MIN_H;
extern const int ADDR_CAL_MIN_L;
extern const int ADDR_CAL_MAX_H;
extern const int ADDR_CAL_MAX_L;

extern const unsigned long MAGIC_NUM;

// Инициализация оборудования
void hardwareSetup();

// Работа с датчиком
int readSensorRaw(); // Сырое значение АЦП
bool isSensorError(); // Проверка обрыва

// Работа с реле
void setValveState(int valveNum, bool state);
bool getValveState(int valveNum);

// Работа с EEPROM
void saveSettings();
void loadSettings();
void resetSettings();