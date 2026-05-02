#include "hardware.h"
#include "logic.h"

// Состояние реле
static bool valve1State = false;
static bool valve2State = false;

// Флаг ошибки датчика — доступен через extern из logic/ui
bool sensorErrorFlag = false;

void hardwareSetup() {
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  digitalWrite(PIN_RELAY_1, LOW);
  digitalWrite(PIN_RELAY_2, LOW);

  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_CHANGE, INPUT_PULLUP);
}

int readSensorRaw() {
  return analogRead(PIN_SENSOR);
}

void setValveState(int valveNum, bool state) {
  if (valveNum == 1) {
    valve1State = state;
    digitalWrite(PIN_RELAY_1, state ? HIGH : LOW);
  } else if (valveNum == 2) {
    valve2State = state;
    digitalWrite(PIN_RELAY_2, state ? HIGH : LOW);
  }
}

bool getValveState(int valveNum) {
  if (valveNum == 1) return valve1State;
  if (valveNum == 2) return valve2State;
  return false;
}

// --- EEPROM ---

void saveSettings() {
  EEPROM.put(ADDR_MAGIC, MAGIC_NUM);
  EEPROM.put(ADDR_RANGE, currentRangeIndex);
  EEPROM.put(ADDR_UNIT, currentUnitIndex);

  int iLow  = (int)(setpointLow * 10);
  int iHigh = (int)(setpointHigh * 10);
  int iHyst = (int)(hysteresis * 10);

  EEPROM.put(ADDR_SP_LOW_H,  highByte(iLow));
  EEPROM.put(ADDR_SP_LOW_L,  lowByte(iLow));
  EEPROM.put(ADDR_SP_HIGH_H, highByte(iHigh));
  EEPROM.put(ADDR_SP_HIGH_L, lowByte(iHigh));
  EEPROM.put(ADDR_HYST_H,    highByte(iHyst));
  EEPROM.put(ADDR_HYST_L,    lowByte(iHyst));

  EEPROM.put(ADDR_CAL_MIN_H, highByte(calMin));
  EEPROM.put(ADDR_CAL_MIN_L, lowByte(calMin));
  EEPROM.put(ADDR_CAL_MAX_H, highByte(calMax));
  EEPROM.put(ADDR_CAL_MAX_L, lowByte(calMax));
}

void loadSettings() {
  unsigned long magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic != MAGIC_NUM) {
    resetSettings();
    return;
  }

  EEPROM.get(ADDR_RANGE, currentRangeIndex);
  EEPROM.get(ADDR_UNIT, currentUnitIndex);

  byte h, l;
  EEPROM.get(ADDR_SP_LOW_H, h);  EEPROM.get(ADDR_SP_LOW_L, l);
  setpointLow = (float)(word(h, l)) / 10.0f;

  EEPROM.get(ADDR_SP_HIGH_H, h); EEPROM.get(ADDR_SP_HIGH_L, l);
  setpointHigh = (float)(word(h, l)) / 10.0f;

  EEPROM.get(ADDR_HYST_H, h);    EEPROM.get(ADDR_HYST_L, l);
  hysteresis = (float)(word(h, l)) / 10.0f;

  EEPROM.get(ADDR_CAL_MIN_H, h); EEPROM.get(ADDR_CAL_MIN_L, l);
  calMin = word(h, l);

  EEPROM.get(ADDR_CAL_MAX_H, h); EEPROM.get(ADDR_CAL_MAX_L, l);
  calMax = word(h, l);

  if (calMin >= calMax) { calMin = 197; calMax = 983; }
}

void resetSettings() {
  currentRangeIndex = 2;
  currentUnitIndex  = 1;
  setpointLow       = 20.0f;
  setpointHigh      = 80.0f;
  hysteresis        = 2.0f;
  calMin            = 197;
  calMax            = 983;

  saveSettings();
}
