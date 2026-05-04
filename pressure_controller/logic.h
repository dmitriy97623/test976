#pragma once

#include "hardware.h"

// --- Константы диапазонов и единиц ---
constexpr int   RANGES_COUNT = 6;
extern const float RANGES[RANGES_COUNT];
constexpr int   UNITS_COUNT  = 5;

// Единицы измерения — массив в logic.cpp
extern const char* const UNIT_NAMES[UNITS_COUNT];

// --- Глобальные переменные (настройки) ---
extern int   currentRangeIndex;
extern int   currentUnitIndex;
extern float setpointLow;
extern float setpointHigh;
extern float hysteresis;
extern int   calMin;
extern int   calMax;

// Инициализация логики
void logicSetup();

// Чтение давления (в выбранных единицах, с учётом калибровки)
float readPressure();

// Управление реле на основе давления и уставок
void controlRelays(float pressure);

// Установить реле в состояние, соответствующее текущему давлению (для старта)
void initRelayStates(float pressure);
