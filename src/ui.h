#pragma once

// Подключение библиотеки для работы с LCD-дисплеем
#include <LiquidCrystal_I2C.h>
// Подключение заголовочного файла модуля logic для доступа к данным
#include "logic.h"

// --- Инициализация ---
// Инициализация аппаратной части дисплея и вывод начального экрана
void uiSetup();

// --- Обработка ввода ---
// Обработка нажатий и удержаний кнопок с антидребезгом
void handleButtons();

// --- Обновление вывода ---
// Центральная функция обновления экрана, выбирает текущий режим отображения
void updateDisplay();

// --- Функции отображения ---
// Отображение рабочего экрана с текущим давлением и состоянием реле
void displayWorkScreen(float pressure);
// Отображение экрана меню
void displayMenuScreen();
// Отображение экрана поразрядного редактирования с мигающим курсором
void displayDigitEditScreen();

// --- Управление меню ---
// Перечисление всех пунктов меню
enum MenuItems { MENU_MODE, MENU_RANGE, MENU_SETPOINT, MENU_HYST, MENU_UNIT, MENU_CALIB, MENU_RESET };
// Текущий выбранный пункт меню
extern MenuItems currentMenuItem;
// Флаг режима редактирования (общий)
extern bool isEditMode;
// Флаг режима поразрядного редактирования чисел
extern bool isDigitEditMode;

// Переход к следующему пункту меню
void nextMenuItem();
// Вход в режим редактирования текущего пункта
void enterEditMode();
// Обработка короткого нажатия на кнопку MENU
void handleShortPress();
// Обработка длинного нажатия на кнопку MENU
void handleLongPress();
// Обработка нажатия на кнопку CHANGE
void handleChangePress();

// --- Портазрядный ввод ---
// Временное значение для редактирования (не используется напрямую)
extern float tempEditValue;
// Текущий активный разряд (0=сотни, 1=десятки, 2=единицы, 3=десятые)
extern int digitIndex;
// Массив цифр для редактируемого числа
extern int digits[4];

// Начало сеанса поразрядного редактирования
void startDigitEdit();
// Переход к следующему разряду
void nextDigit();
// Увеличение значения активного разряда
void incrementDigit();
// Сохранение отредактированного значения
void saveDigitEdit();