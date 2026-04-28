#include "logic.h"

// --- Константы ---
const int RANGES_COUNT = 6;
const float RANGES[RANGES_COUNT] = {1.0, 40.0, 250.0, 400.0, 600.0, 1000.0};
const char* UNIT_NAMES[] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const int UNITS_COUNT = 5;

// Глобальные переменные
int currentRangeIndex = 2;
int currentUnitIndex = 1;
float setpointLow = 20.0;
float setpointHigh = 80.0;
float hysteresis = 2.0;
int calMin = 197;
int calMax = 983;

void logicSetup() {
  // Инициализация не требуется
}

float readPressure() {
  int val = readSensorRaw();

  if (val < (calMin - 20)) {
    // Обрыв линии
    sensorErrorFlag = true;
    return 0.0;
  }
  sensorErrorFlag = false;

  if (val < calMin) val = calMin;
  if (val > calMax) val = calMax;

  float percent = (float)(val - calMin) / (float)(calMax - calMin);
  float rangeVal = RANGES[currentRangeIndex];
  return percent * rangeVal;
}

void controlRelays(float pressure) {
  if (isSensorError()) {
    setValveState(1, false);
    setValveState(2, false);
    return;
  }

  // Реле 1: включается при низком давлении
  if (pressure < (setpointLow - hysteresis)) setValveState(1, true);
  if (pressure > setpointLow) setValveState(1, false);

  // Реле 2: включается при высоком давлении
  if (pressure > (setpointHigh + hysteresis)) setValveState(2, true);
  if (pressure < setpointHigh) setValveState(2, false);
}