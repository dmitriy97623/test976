#pragma once

#include <LiquidCrystal_I2C.h>
#include "logic.h"

// Глобальный объект дисплея
extern LiquidCrystal_I2C lcd;

// --- Состояния UI (машина состояний) ---
enum UiState {
  STATE_WORK,       // Рабочий экран: давление + состояние реле
  STATE_BROWSE,     // Навигация по меню настроек
  STATE_EDIT_LIST,  // Редактирование выбором из списка (Range, Unit)
  STATE_EDIT_DIGIT, // Поразрядный ввод числа (Setpoint, Hyst)
  STATE_CONFIRM     // Подтверждение опасного действия (Reset, Calibr)
};

// --- Пункты меню ---
enum MenuItems {
  MENU_RANGE,       // Диапазон датчика
  MENU_SP_LOW,      // Нижняя уставка
  MENU_SP_HIGH,     // Верхняя уставка
  MENU_HYST,        // Гистерезис
  MENU_UNIT,        // Единицы измерения
  MENU_CALIB,       // Калибровка
  MENU_RESET,       // Сброс настроек
  MENU_COUNT
};

// Инициализация UI
void uiSetup();
void uiClear();

// Обработка кнопок
void handleButtons();

// Обновление экрана (вызывается из loop, давление передаётся извне)
void updateDisplay(float pressure);

// Запросы состояния
bool isInMenu();       // true — устройство в меню (реле управляются, но экран не рабочий)
bool isCalibrating();  // true — идёт калибровка (нужна особая логика)
