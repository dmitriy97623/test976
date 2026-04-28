#pragma once

#include <LiquidCrystal_I2C.h>
#include "logic.h"

// Инициализация дисплея
void uiSetup();

// Обработка кнопок
void handleButtons();

// Обновление экрана
void updateDisplay();

// Режимы отображения
void displayWorkScreen(float pressure);
void displayMenuScreen();
void displayDigitEditScreen();

// Управление меню
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_HYST, MENU_UNIT, MENU_CALIB, MENU_RESET };
extern MenuItems currentMenuItem;
extern bool isEditMode;
extern bool isDigitEditMode;

void nextMenuItem();
void enterEditMode();
void handleShortPress();
void handleLongPress();
void handleChangePress();

// Портазрядный ввод
extern float tempEditValue;
extern int digitIndex;
extern int digits[4];

void startDigitEdit();
void nextDigit();
void incrementDigit();
void saveDigitEdit();