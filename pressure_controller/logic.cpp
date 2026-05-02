#include "logic.h"

// --- Константы ---
const char* const UNIT_NAMES[UNITS_COUNT] = {"Pa", "kPa", "MPa", "bar", "mbar"};

// --- Глобальные переменные ---
int   currentRangeIndex = 2;
int   currentUnitIndex  = 1;
float setpointLow       = 20.0f;
float setpointHigh      = 80.0f;
float hysteresis        = 2.0f;
int   calMin            = 197;
int   calMax            = 983;

void logicSetup() {
  // Инициализация не требуется
}

float readPressure() {
  int val = readSensorRaw();

  if (val < (calMin - 20)) {
    sensorErrorFlag = true;
    return 0.0f;
  }
  sensorErrorFlag = false;

  if (val < calMin) val = calMin;
  if (val > calMax) val = calMax;

  float percent  = (float)(val - calMin) / (float)(calMax - calMin);
  float rangeVal = RANGES[currentRangeIndex];
  return percent * rangeVal;
}

void controlRelays(float pressure) {
  if (sensorErrorFlag) {
    setValveState(1, false);
    setValveState(2, false);
    return;
  }

  // Реле 1: включается при низком давлении (ниже уставки минус гистерезис)
  if (pressure < (setpointLow - hysteresis)) setValveState(1, true);
  if (pressure > setpointLow)               setValveState(1, false);

  // Реле 2: включается при высоком давлении (выше уставки плюс гистерезис)
  if (pressure > (setpointHigh + hysteresis)) setValveState(2, true);
  if (pressure < setpointHigh)                setValveState(2, false);
}

void initRelayStates(float pressure) {
  if (sensorErrorFlag) {
    setValveState(1, false);
    setValveState(2, false);
    return;
  }

  // Однозначно определяем состояние при старте
  setValveState(1, pressure < setpointLow);
  setValveState(2, pressure > setpointHigh);
}
