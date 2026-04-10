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

#ifndef DW1000_DRIVER_H
#define DW1000_DRIVER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Адреса регистров (основные, согласно документации) */
#define DW1000_DEV_ID       0x00    // Идентификатор устройства
#define DW1000_EUI          0x01    // Extended Unique Identifier
#define DW1000_PANADR       0x03    // PAN ID и короткий адрес
#define DW1000_SYS_CFG      0x04    // Системная конфигурация
#define DW1000_SYS_TIME     0x06    // Системный таймер (40-bit)
#define DW1000_TX_FCTRL     0x08    // Управление передачей кадра
#define DW1000_TX_BUFFER    0x09    // Буфер передачи
#define DW1000_DX_TIME      0x0A    // Время отложенной отправки/приема
#define DW1000_SYS_CTRL     0x0D    // Управление системой
#define DW1000_SYS_MASK     0x0E    // Маска событий
#define DW1000_SYS_STATUS   0x0F    // Статус системы
#define DW1000_RX_FINFO     0x10    // Информация о принятом кадре
#define DW1000_RX_BUFFER    0x11    // Буфер приема
#define DW1000_RX_FQUAL     0x12    // Качество принятого кадра
#define DW1000_RX_TIME      0x15    // Временная метка приема
#define DW1000_TX_TIME      0x17    // Временная метка передачи
#define DW1000_TX_ANTD      0x18    // Задержка антенны передачи
#define DW1000_CHAN_CTRL    0x1F    // Управление каналом
#define DW1000_OTP_IF       0x2D    // Интерфейс OTP памяти
#define DW1000_PMSC         0x36    // Управление питанием

/* Структура для описания экземпляра DW1000 */
typedef struct {
    SPI_HandleTypeDef* spi;          // Указатель на SPI периферию
    GPIO_TypeDef* cs_port;            // Порт для CS
    uint16_t cs_pin;                  // Пин для CS
    GPIO_TypeDef* irq_port;           // Порт для IRQ
    uint16_t irq_pin;                 // Пин для IRQ
    GPIO_TypeDef* rst_port;           // Порт для RST
    uint16_t rst_pin;                 // Пин для RST
    
    /* Кэш конфигурации */
    uint8_t channel;
    uint8_t data_rate;
    uint16_t preamble_len;
    uint8_t prf;
} DW1000_Device;

/* Коды ошибок */
typedef enum {
    DW1000_OK = 0,
    DW1000_ERROR_SPI,
    DW1000_ERROR_TIMEOUT,
    DW1000_ERROR_INVALID_PARAM,
    DW1000_ERROR_NOT_INITIALIZED
} DW1000_Status;

/* Прототипы функций */
DW1000_Status DW1000_Init(DW1000_Device* dev);
DW1000_Status DW1000_ReadRegister(DW1000_Device* dev, uint8_t reg_id, 
                                   uint16_t sub_addr, uint8_t* data, uint16_t len);
DW1000_Status DW1000_WriteRegister(DW1000_Device* dev, uint8_t reg_id, 
                                    uint16_t sub_addr, const uint8_t* data, uint16_t len);
DW1000_Status DW1000_ReadRegister8(DW1000_Device* dev, uint8_t reg_id, 
                                    uint16_t sub_addr, uint8_t* value);
DW1000_Status DW1000_WriteRegister8(DW1000_Device* dev, uint8_t reg_id, 
                                     uint16_t sub_addr, uint8_t value);
DW1000_Status DW1000_ReadRegister32(DW1000_Device* dev, uint8_t reg_id, 
                                     uint16_t sub_addr, uint32_t* value);
DW1000_Status DW1000_WriteRegister32(DW1000_Device* dev, uint8_t reg_id, 
                                      uint16_t sub_addr, uint32_t value);
DW1000_Status DW1000_ReadOTP(DW1000_Device* dev, uint16_t otp_addr, uint32_t* value);
DW1000_Status DW1000_LoadLDE(DW1000_Device* dev);
DW1000_Status DW1000_SoftReset(DW1000_Device* dev);
void DW1000_IRQHandler(DW1000_Device* dev);

#endif /* DW1000_DRIVER_H */