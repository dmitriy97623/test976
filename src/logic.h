#pragma once

#include "hardware.h"

// --- Константы ---
extern const int RANGES_COUNT;
extern const float RANGES[];
extern const char* UNIT_NAMES[];
extern const int UNITS_COUNT;

// Глобальные переменные (настройки)
extern int currentRangeIndex;
extern int currentUnitIndex;
extern float setpointLow;
extern float setpointHigh;
extern float hysteresis;
extern int calMin;
extern int calMax;

// Инициализация логики
void logicSetup();

// Чтение давления (в выбранных единицах)
float readPressure();

// Управление состоянием реле на основе давления и уставок
void controlRelays(float pressure);