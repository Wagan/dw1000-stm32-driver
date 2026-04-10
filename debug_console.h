/***************************************************************
*             Библиотека функций для работы с DW1000           *
*                                                              *
*                  Версия 1.3.01                               *
*                                                              *
*             Согласовано со следующими документами:           *
*                 - МКС API v.1.3.pdf                          *
*                 - Бинарный протокол МКС v.1.3.pdf            *
*                 - DW1000 User Manual DecaWave v.2.17         *
*                                                              *
*                  Copyright (C) NCPR LLC                      *
*                    https://flexlab.ru                        *
****************************************************************/

#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

/* Инициализация консоли */
void DEBUG_CONSOLE_Init(UART_HandleTypeDef* huart);

/* Задача консоли (для FreeRTOS) */
void DEBUG_Console_Task(void* pvParameters);

/* Функции вывода информации (можно вызывать из любого места) */
void DEBUG_Print(const char* str);
void DEBUG_Println(const char* str);
void DEBUG_Printf(const char* format, ...);
void DEBUG_PrintHex(const uint8_t* data, uint16_t len);
void DEBUG_PrintStatus(void);  // печать текущего состояния системы

/* Уровни отладочных сообщений */
typedef enum {
    DEBUG_LEVEL_ERROR = 0,
    DEBUG_LEVEL_WARNING,
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_DEBUG,
    DEBUG_LEVEL_VERBOSE
} DebugLevel;

void DEBUG_SetLevel(DebugLevel level);
void DEBUG_SetUART(UART_HandleTypeDef* huart);

#endif /* DEBUG_CONSOLE_H */