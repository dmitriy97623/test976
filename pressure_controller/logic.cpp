#include "logic.h"
#include "hardware.h"

// ============================================================================
// Константы
// ============================================================================

const char* const UNIT_NAMES[UNITS_COUNT] = {"Pa", "kPa", "MPa", "bar", "mbar"};
const float RANGES[RANGES_COUNT] = {1.0f, 40.0f, 250.0f, 400.0f, 600.0f, 1000.0f};

// ============================================================================
// Глобальные переменные
// ============================================================================

byte  currentRangeIndex = 2;
byte  currentUnitIndex  = 1;
float setpointLow       = 20.0f;
float setpointHigh      = 80.0f;
float hysteresis        = 2.0f;
int   calMin            = 197;
int   calMax            = 983;

// ============================================================================
// Инициализация
// ============================================================================

void logicSetup() {
  // Инициализация не требуется
}

// ============================================================================
// Чтение давления
// ============================================================================

float readPressure() {
  // Чтение с усреднением для снижения шума
  int val = readSensorFiltered(16);  // 16 выборок

  // Проверка на обрыв линии (ток < 4 мА)
  // calMin - 20 — допуск на шум
  if (val < (calMin - 20)) {
    sensorErrorFlag = true;
    return 0.0f;
  }
  sensorErrorFlag = false;

  // Ограничение по калибровочным границам
  if (val < calMin) val = calMin;
  if (val > calMax) val = calMax;

  // Расчёт давления
  float percent  = (float)(val - calMin) / (float)(calMax - calMin);
  float rangeVal = RANGES[currentRangeIndex];
  return percent * rangeVal;
}

// ============================================================================
// Управление реле
// ============================================================================

void controlRelays(float pressure) {
  if (sensorErrorFlag) {
    setValveState(1, false);
    setValveState(2, false);
    return;
  }

  // Логика управления реле:
  // - Давление < (setpointLow - hysteresis): Реле 1 ОТКРЫТО (накачивать), Реле 2 ЗАКРЫТО
  // - Давление в диапазоне [setpointLow - hysteresis, setpointHigh + hysteresis]: ОБА ЗАКРЫТЫ
  // - Давление > (setpointHigh + hysteresis): Реле 1 ЗАКРЫТО, Реле 2 ОТКРЫТО (сбрасывать)

  if (pressure < (setpointLow - hysteresis)) {
    // Слишком низкое давление — накачивать
    setValveState(1, true);
    setValveState(2, false);
  } else if (pressure > (setpointHigh + hysteresis)) {
    // Слишком высокое давление — сбрасывать
    setValveState(1, false);
    setValveState(2, true);
  } else {
    // Давление в норме — оба реле закрыты
    setValveState(1, false);
    setValveState(2, false);
  }
}

void initRelayStates(float pressure) {
  if (sensorErrorFlag) {
    setValveState(1, false);
    setValveState(2, false);
    return;
  }

  // Однозначно определяем состояние при старте
  setValveState(1, false); // Выключить при старте
  setValveState(2, false); // Выключить при старте
}
