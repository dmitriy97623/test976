#pragma once

// Стандартные библиотеки
#include <Arduino.h>
#include <EEPROM.h>

// --- Конфигурация пинов ---
// Определение констант для подключения периферии к пинам Arduino
extern const int PIN_SENSOR;         // Аналоговый пин для сигнала датчика давления (4-20 мА)
extern const int PIN_RELAY_1;       // Цифровой пин для управления первым реле (Клапан 1)
extern const int PIN_RELAY_2;       // Цифровой пин для управления вторым реле (Клапан 2)
extern const int PIN_BTN_MENU;      // Цифровой пин для кнопки MENU (с подтяжкой к VCC)
extern const int PIN_BTN_CHANGE;    // Цифровой пин для кнопки CHANGE (с подтяжкой к VCC)

// --- Адреса в EEPROM ---
// Константы с адресами для хранения настроек в энергонезависимой памяти
extern const int ADDR_MAGIC;         // Адрес магического числа (идентификатор версии/инициализации)
extern const int ADDR_RANGE;        // Адрес для хранения индекса текущего диапазона
extern const int ADDR_UNIT;          // Адрес для хранения индекса единиц измерения
extern const int ADDR_SP_LOW_H;      // Старший байт нижней уставки давления
extern const int ADDR_SP_LOW_L;      // Младший байт нижней уставки давления
extern const int ADDR_SP_HIGH_H;     // Старший байт верхней уставки давления
extern const int ADDR_SP_HIGH_L;     // Младший байт верхней уставки давления
extern const int ADDR_HYST_H;        // Старший байт значения гистерезиса
extern const int ADDR_HYST_L;        // Младший байт значения гистерезиса
extern const int ADDR_CAL_MIN_H;     // Старший байт калибровочного значения для 4 мА
extern const int ADDR_CAL_MIN_L;     // Младший байт калибровочного значения для 4 мА
extern const int ADDR_CAL_MAX_H;     // Старший байт калибровочного значения для 20 мА
extern const int ADDR_CAL_MAX_L;     // Младший байт калибровочного значения для 20 мА

extern const unsigned long MAGIC_NUM; // Контрольное число для проверки корректности данных в EEPROM

// --- Функции инициализации ---
// Инициализация всех аппаратных пинов
void hardwareSetup();

// --- Функции работы с датчиком ---
// Чтение сырого значения с АЦП (0-1023)
int readSensorRaw();
// Проверка состояния датчика (обрыв линии)
bool isSensorError();

// --- Функции работы с реле ---
// Установка состояния реле (1 или 2)
void setValveState(int valveNum, bool state);
// Получение текущего состояния реле
bool getValveState(int valveNum);

// --- Функции работы с EEPROM ---
// Сохранение всех настроек в EEPROM
void saveSettings();
// Загрузка всех настроек из EEPROM
void loadSettings();
// Сброс всех настроек к значениям по умолчанию
void resetSettings();