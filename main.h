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

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "radio_defs.h"

/* Структура для передачи данных через USB очередь */
typedef struct {
    uint8_t* data;
    uint16_t len;
} TxBuffer_t;


/* Глобальные объекты синхронизации */
extern SemaphoreHandle_t xTxCompleteSemaphore;
// ... остальные extern'ы ...
/* Глобальные объекты синхронизации */
extern SemaphoreHandle_t xTxCompleteSemaphore;
extern SemaphoreHandle_t xRxCompleteSemaphore;
extern QueueHandle_t xRadioCommandQueue;
extern QueueHandle_t xRadioEventQueue;
extern QueueHandle_t xUSB_TxQueue;   // очередь из TxBuffer_t*

/* Прототипы функций инициализации (генерируются CubeMX) */
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_SPI1_Init(void);
void MX_USB_DEVICE_Init(void);

/* Функции для работы с USB CDC (определены в usbd_cdc_if.c) */
uint8_t CDC_ReceiveByte(uint8_t* byte);
void CDC_Transmit(uint8_t* buf, uint16_t len);

/* Для отладочной консоли */
extern QueueHandle_t xConsoleCommandQueue;
void DEBUG_Printf(const char* format, ...);  // можно использовать из любого места

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */