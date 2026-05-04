#include "hardware.h"
#include "logic.h"

// ============================================================================
// Глобальные переменные
// ============================================================================

bool sensorErrorFlag = false;
bool watchdogResetFlag = false;

// Состояние реле
static bool valve1State = false;
static bool valve2State = false;

// Предыдущие значения для сравнения при сохранении
static byte prevRangeIndex = 255;
static byte prevUnitIndex = 255;
static int prevSpLow_H = -1, prevSpLow_L = -1;
static int prevSpHigh_H = -1, prevSpHigh_L = -1;
static int prevHyst_H = -1, prevHyst_L = -1;

// ============================================================================
// Watchdog Timer
// ============================================================================

void wdtSetup() {
  // Таймаут 2 секунды (WDT_CSR = WDP3 + WDP0 = 2.0s)
  wdt_enable(WDTO_2S);
}

void wdtReset() {
  wdt_reset();
}

void clearWdtResetFlag() {
  watchdogResetFlag = false;
  EEPROM.write(ADDR_WDT_FLAG, 0);
}

static bool checkWdtResetFlag() {
  return EEPROM.read(ADDR_WDT_FLAG) == 1;
}

// ============================================================================
// Инициализация оборудования
// ============================================================================

void hardwareSetup() {
  // Отключаем не используемые периферийные модули для экономии энергии
  #if defined(AVR_POWER_H_)
    power_spi_disable();
    power_twi_disable();
    power_usart_disable();
    power_timer0_disable();  // Внимание: millis() не будет работать!
  #endif
  
  // Пины реле
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  digitalWrite(PIN_RELAY_1, LOW);
  digitalWrite(PIN_RELAY_2, LOW);

  // Пины кнопок (внутренние подтяжки)
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_CHANGE, INPUT_PULLUP);

  // Аналоговый вход датчика
  analogReference(DEFAULT);  // AVCC (5В)

  // Проверка сброса от Watchdog
  watchdogResetFlag = checkWdtResetFlag();
  if (watchdogResetFlag) {
    // Помечаем, что сейчас нормальный режим (сброс был раньше)
    EEPROM.write(ADDR_WDT_FLAG, 0);
  }

  // Инициализация Watchdog (опционально, можно включить позже)
  // wdtSetup();
}

// ============================================================================
// АЦП — чтение с усреднением и шумоподавлением
// ============================================================================

int readSensorRaw() {
  // Стандартное чтение A0
  return analogRead(PIN_SENSOR);
}

int readSensorFiltered(uint8_t samples) {
  if (samples == 0) samples = 1;
  
  uint32_t sum = 0;
  
  // Режим шумоподавления ADC перед чтением
  // (если не используется sleep, просто делаем задержку)
  for (uint8_t i = 0; i < samples; i++) {
    sum += readSensorRaw();
    
    // Небольшая задержка для стабилизации сигнала
    if (samples > 1) {
      delayMicroseconds(100);  // 100 мкс между выборками
    }
  }
  
  return (int)(sum / samples);
}

// ============================================================================
// Реле
// ============================================================================

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

// ============================================================================
// EEPROM — сохранение с защитой от частых записей
// ============================================================================

static void saveInt16(int addr, int value) {
  EEPROM.write(addr, highByte(value));
  EEPROM.write(addr + 1, lowByte(value));
}

static int loadInt16(int addr) {
  byte h = EEPROM.read(addr);
  byte l = EEPROM.read(addr + 1);
  return word(h, l);
}

bool shouldSaveSettings() {
  // Сравниваем текущие значения с предыдущими
  int curLow_H = highByte((int)(setpointLow * 10));
  int curLow_L = lowByte((int)(setpointLow * 10));
  int curHigh_H = highByte((int)(setpointHigh * 10));
  int curHigh_L = lowByte((int)(setpointHigh * 10));
  int curHyst_H = highByte((int)(hysteresis * 10));
  int curHyst_L = lowByte((int)(hysteresis * 10));

  bool changed = (currentRangeIndex != prevRangeIndex) ||
                 (currentUnitIndex != prevUnitIndex) ||
                 (curLow_H != prevSpLow_H) || (curLow_L != prevSpLow_L) ||
                 (curHigh_H != prevSpHigh_H) || (curHigh_L != prevSpHigh_L) ||
                 (curHyst_H != prevHyst_H) || (curHyst_L != prevHyst_L);

  // Обновляем предыдущие значения
  prevRangeIndex = currentRangeIndex;
  prevUnitIndex = currentUnitIndex;
  prevSpLow_H = curLow_H;
  prevSpLow_L = curLow_L;
  prevSpHigh_H = curHigh_H;
  prevSpHigh_L = curHigh_L;
  prevHyst_H = curHyst_H;
  prevHyst_L = curHyst_L;

  return changed;
}

void saveSettings() {
  // Проверяем, есть ли изменения
  if (!shouldSaveSettings()) return;

  EEPROM.put(ADDR_MAGIC, MAGIC_NUM);
  EEPROM.write(ADDR_RANGE, currentRangeIndex);
  EEPROM.write(ADDR_UNIT, currentUnitIndex);

  int iLow  = (int)(setpointLow * 10);
  int iHigh = (int)(setpointHigh * 10);
  int iHyst = (int)(hysteresis * 10);

  saveInt16(ADDR_SP_LOW_H, iLow);
  saveInt16(ADDR_SP_HIGH_H, iHigh);
  saveInt16(ADDR_HYST_H, iHyst);

  saveInt16(ADDR_CAL_MIN_H, calMin);
  saveInt16(ADDR_CAL_MAX_H, calMax);
}

void loadSettings() {
  unsigned long magic;
  EEPROM.get(ADDR_MAGIC, magic);
  // Проверка версии EEPROM
  byte version = EEPROM.read(ADDR_VERSION);
  if (version != EEPROM_VERSION) {
    resetSettings();
    return;
  }

  if (magic != MAGIC_NUM) {
    resetSettings();
    return;
  }

  currentRangeIndex = (byte)EEPROM.read(ADDR_RANGE);
  currentUnitIndex = (byte)EEPROM.read(ADDR_UNIT);

  calMin = loadInt16(ADDR_CAL_MIN_H);
  calMax = loadInt16(ADDR_CAL_MAX_H);

  // Временно загружаем уставок без проверки на изменение
  int iLow = loadInt16(ADDR_SP_LOW_H);
  int iHigh = loadInt16(ADDR_SP_HIGH_H);
  int iHyst = loadInt16(ADDR_HYST_H);

  setpointLow = (float)iLow / 10.0f;
  setpointHigh = (float)iHigh / 10.0f;
  hysteresis = (float)iHyst / 10.0f;

  if (calMin >= calMax) { calMin = 197; calMax = 983; }

  // Проверка и исправление границ диапазонов
  if (currentRangeIndex >= RANGES_COUNT) currentRangeIndex = 2;
  if (currentUnitIndex >= UNITS_COUNT) currentUnitIndex = 1;

  // Инициализируем предыдущие значения
  prevRangeIndex = currentRangeIndex;
  prevUnitIndex = currentUnitIndex;
  prevSpLow_H = highByte(iLow);
  prevSpLow_L = lowByte(iLow);
  prevSpHigh_H = highByte(iHigh);
  prevSpHigh_L = lowByte(iHigh);
  prevHyst_H = highByte(iHyst);
  prevHyst_L = lowByte(iHyst);
}

void resetSettings() {
  currentRangeIndex = 2;
  currentUnitIndex  = 1;
  setpointLow       = 20.0f;
  setpointHigh      = 80.0f;
  hysteresis        = 2.0f;
  calMin            = 197;
  calMax            = 983;

  // Сброс предыдущих значений
  prevRangeIndex = 255;
  prevUnitIndex = 255;

  EEPROM.write(ADDR_VERSION, EEPROM_VERSION);
  saveSettings();
}
