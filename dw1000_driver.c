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

/**
 * @file dw1000_driver.c
 * @brief Драйвер для работы с модулем DW1000 (Decawave)
 *
 * Реализует низкоуровневые операции SPI, инициализацию, загрузку LDE,
 * программный сброс, а также обработку прерываний с синхронизацией FreeRTOS.
 */

#include "debug_console.h"
#include "dw1000_driver.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Внешние объекты синхронизации (определены в main.c)
 * -------------------------------------------------------------------------- */
extern SemaphoreHandle_t xTxCompleteSemaphore;
extern SemaphoreHandle_t xRxCompleteSemaphore;
extern QueueHandle_t xRadioEventQueue;

/* --------------------------------------------------------------------------
 * Локальные функции
 * -------------------------------------------------------------------------- */

static uint8_t DW1000_BuildHeader(uint8_t reg_id, uint16_t sub_addr,
                                   bool is_write, uint8_t* header)
{
    uint8_t header_len = 1;
    header[0] = (reg_id & 0x3F);
    if (is_write) header[0] |= 0x80;
    if (sub_addr == 0) {
        header[0] &= 0xBF;
        return 1;
    }
    header[0] |= 0x40;
    if (sub_addr < 128) {
        header[1] = sub_addr & 0x7F;
        header_len = 2;
    } else {
        header[1] = 0x80 | (sub_addr & 0x7F);
        header[2] = (sub_addr >> 7) & 0xFF;
        header_len = 3;
    }
    return header_len;
}

static void DW1000_EnterCritical(void)
{
    taskENTER_CRITICAL();
}

static void DW1000_ExitCritical(void)
{
    taskEXIT_CRITICAL();
}

/* --------------------------------------------------------------------------
 * Основные SPI операции
 * -------------------------------------------------------------------------- */

DW1000_Status DW1000_ReadRegister(DW1000_Device* dev, uint8_t reg_id,
                                   uint16_t sub_addr, uint8_t* data, uint16_t len)
{
    uint8_t header[3];
    uint8_t header_len;
    HAL_StatusTypeDef hal_status;

    if (dev == NULL || data == NULL) return DW1000_ERROR_INVALID_PARAM;

    header_len = DW1000_BuildHeader(reg_id, sub_addr, false, header);

    DW1000_EnterCritical();
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
    hal_status = HAL_SPI_Transmit(dev->spi, header, header_len, HAL_MAX_DELAY);
    if (hal_status != HAL_OK) {
        HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
        DW1000_ExitCritical();
        return DW1000_ERROR_SPI;
    }
    if (len > 0) {
        hal_status = HAL_SPI_Receive(dev->spi, data, len, HAL_MAX_DELAY);
        if (hal_status != HAL_OK) {
            HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
            DW1000_ExitCritical();
            return DW1000_ERROR_SPI;
        }
    }
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
    DW1000_ExitCritical();
    return DW1000_OK;
}

DW1000_Status DW1000_WriteRegister(DW1000_Device* dev, uint8_t reg_id,
                                    uint16_t sub_addr, const uint8_t* data, uint16_t len)
{
    uint8_t header[3];
    uint8_t header_len;
    HAL_StatusTypeDef hal_status;
    uint8_t* tx_buffer;
    uint16_t total_len;

    if (dev == NULL || data == NULL) return DW1000_ERROR_INVALID_PARAM;

    header_len = DW1000_BuildHeader(reg_id, sub_addr, true, header);
    total_len = header_len + len;
    tx_buffer = malloc(total_len);
    if (tx_buffer == NULL) return DW1000_ERROR_INVALID_PARAM;

    memcpy(tx_buffer, header, header_len);
    memcpy(tx_buffer + header_len, data, len);

    DW1000_EnterCritical();
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
    hal_status = HAL_SPI_Transmit(dev->spi, tx_buffer, total_len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
    DW1000_ExitCritical();

    free(tx_buffer);
    return (hal_status == HAL_OK) ? DW1000_OK : DW1000_ERROR_SPI;
}

/* Удобные обертки */
DW1000_Status DW1000_ReadRegister8(DW1000_Device* dev, uint8_t reg_id,
                                    uint16_t sub_addr, uint8_t* value)
{
    return DW1000_ReadRegister(dev, reg_id, sub_addr, value, 1);
}

DW1000_Status DW1000_WriteRegister8(DW1000_Device* dev, uint8_t reg_id,
                                     uint16_t sub_addr, uint8_t value)
{
    return DW1000_WriteRegister(dev, reg_id, sub_addr, &value, 1);
}

DW1000_Status DW1000_ReadRegister16(DW1000_Device* dev, uint8_t reg_id,
                                     uint16_t sub_addr, uint16_t* value)
{
    uint8_t buf[2];
    DW1000_Status status = DW1000_ReadRegister(dev, reg_id, sub_addr, buf, 2);
    if (status == DW1000_OK) {
        *value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    }
    return status;
}

DW1000_Status DW1000_WriteRegister16(DW1000_Device* dev, uint8_t reg_id,
                                      uint16_t sub_addr, uint16_t value)
{
    uint8_t buf[2];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    return DW1000_WriteRegister(dev, reg_id, sub_addr, buf, 2);
}

DW1000_Status DW1000_ReadRegister32(DW1000_Device* dev, uint8_t reg_id,
                                     uint16_t sub_addr, uint32_t* value)
{
    uint8_t buf[4];
    DW1000_Status status = DW1000_ReadRegister(dev, reg_id, sub_addr, buf, 4);
    if (status == DW1000_OK) {
        *value = (uint32_t)buf[0] |
                ((uint32_t)buf[1] << 8) |
                ((uint32_t)buf[2] << 16) |
                ((uint32_t)buf[3] << 24);
    }
    return status;
}

DW1000_Status DW1000_WriteRegister32(DW1000_Device* dev, uint8_t reg_id,
                                      uint16_t sub_addr, uint32_t value)
{
    uint8_t buf[4];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
    return DW1000_WriteRegister(dev, reg_id, sub_addr, buf, 4);
}

/* --------------------------------------------------------------------------
 * Специализированные функции инициализации
 * -------------------------------------------------------------------------- */

DW1000_Status DW1000_LoadLDE(DW1000_Device* dev)
{
    DW1000_Status status;

    status = DW1000_WriteRegister32(dev, DW1000_PMSC, 0x00, 0x0301);
    if (status != DW1000_OK) 
{
    DEBUG_Printf("[DW1000] Ошибка: %d\r\n", status);
    return status;
}

    status = DW1000_WriteRegister32(dev, DW1000_OTP_IF, 0x06, 0x8000);
    if (status != DW1000_OK)
{
    DEBUG_Printf("[DW1000] Ошибка: %d\r\n", status);
    return status;
}

    HAL_Delay(1);  // минимум 150 мкс

    status = DW1000_WriteRegister32(dev, DW1000_PMSC, 0x00, 0x0200);
    return status;
}

DW1000_Status DW1000_SoftReset(DW1000_Device* dev)
{
    uint32_t pmsc_ctrl;
    DW1000_Status status;

    status = DW1000_ReadRegister32(dev, DW1000_PMSC, 0x00, &pmsc_ctrl);
    if (status != DW1000_OK) 
{
    DEBUG_Printf("[DW1000] Ошибка: %d\r\n", status);
    return status;
}

    pmsc_ctrl &= 0x0FFFFFFF;
    status = DW1000_WriteRegister32(dev, DW1000_PMSC, 0x00, pmsc_ctrl);
    if (status != DW1000_OK) 
{
    DEBUG_Printf("[DW1000] Ошибка: %d\r\n", status);
    return status;
}

    HAL_Delay(1);

    pmsc_ctrl |= 0xF0000000;
    status = DW1000_WriteRegister32(dev, DW1000_PMSC, 0x00, pmsc_ctrl);
    return status;
}

DW1000_Status DW1000_Init(DW1000_Device* dev)
{
    DW1000_Status status;
    uint32_t dev_id;

    if (dev == NULL)     return DW1000_ERROR_INVALID_PARAM;

    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    HAL_Delay(10);

    status = DW1000_ReadRegister32(dev, DW1000_DEV_ID, 0, &dev_id);
    if (status != DW1000_OK) return status;
    if ((dev_id & 0xFFFF0000) != 0xDECA0000) return DW1000_ERROR_INVALID_PARAM;

    status = DW1000_LoadLDE(dev);
    if (status != DW1000_OK) return status;

    /* Здесь можно добавить настройки по умолчанию: канал, мощность и т.д. */

    return DW1000_OK;
}

DW1000_Status DW1000_ReadOTP(DW1000_Device* dev, uint16_t otp_addr, uint32_t* value)
{
    DW1000_Status status;
    status = DW1000_WriteRegister16(dev, DW1000_OTP_IF, 0x04, otp_addr);
    if (status != DW1000_OK) return status;
    status = DW1000_WriteRegister8(dev, DW1000_OTP_IF, 0x06, 0x03);
    if (status != DW1000_OK) return status;
    HAL_Delay(1);
    status = DW1000_ReadRegister32(dev, DW1000_OTP_IF, 0x0A, value);
    DW1000_WriteRegister8(dev, DW1000_OTP_IF, 0x06, 0x00);
    return status;
}

/* --------------------------------------------------------------------------
 * Обработчик прерываний от DW1000
 * -------------------------------------------------------------------------- */

void DW1000_IRQHandler(DW1000_Device* dev)
{
    uint32_t sys_status;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    RadioEvent_t evt;

    if (dev == NULL) return;

    if (DW1000_ReadRegister32(dev, DW1000_SYS_STATUS, 0, &sys_status) != DW1000_OK) {
        return;
    }

    /* Очищаем все флаги, которые будут обработаны */
    uint32_t clear_mask = 0;

    /* TXFRS (бит 7) - передача завершена */
    if (sys_status & 0x0080) {
        clear_mask |= 0x0080;
        evt.dev = dev;
        evt.event = RADIO_EVT_TX_DONE;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
        xSemaphoreGiveFromISR(xTxCompleteSemaphore, &xHigherPriorityTaskWoken);
    }

    /* RXFCG (бит 14) - успешный приём */
    if (sys_status & 0x4000) {
        clear_mask |= 0x4000;
        evt.dev = dev;
        evt.event = RADIO_EVT_RX_DONE;
        /* Здесь можно прочитать дополнительные регистры и заполнить evt.info.rx_done */
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
        xSemaphoreGiveFromISR(xRxCompleteSemaphore, &xHigherPriorityTaskWoken);
    }

    /* RXPTO (бит 5) - таймаут преамбулы */
    if (sys_status & 0x0020) {
        clear_mask |= 0x0020;
        evt.dev = dev;
        evt.event = RADIO_EVT_RX_TIMEOUT;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
    }

    /* RXPHE (бит 12) - ошибка PHR */
    if (sys_status & 0x1000) {
        clear_mask |= 0x1000;
        evt.dev = dev;
        evt.event = RADIO_EVT_RX_PHR_ERROR;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
    }

    /* RXOVRR (бит 20) - переполнение приёмника */
    if (sys_status & 0x100000) {
        clear_mask |= 0x100000;
        evt.dev = dev;
        evt.event = RADIO_EVT_RX_OVERRUN;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
    }

    /* AFFREJ (бит 29) - отклонение фильтром */
    if (sys_status & 0x20000000) {
        clear_mask |= 0x20000000;
        evt.dev = dev;
        evt.event = RADIO_EVT_FRAME_REJECTED;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
    }

    /* TXBERR (бит 28) - ошибка буфера передачи */
    if (sys_status & 0x10000000) {
        clear_mask |= 0x10000000;
        evt.dev = dev;
        evt.event = RADIO_EVT_TX_BUFFER_ERROR;
        xQueueSendFromISR(xRadioEventQueue, &evt, &xHigherPriorityTaskWoken);
    }

    /* Очищаем обработанные флаги */
    if (clear_mask != 0) {
        DW1000_WriteRegister32(dev, DW1000_SYS_STATUS, 0, clear_mask);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* --------------------------------------------------------------------------
 * Дополнительные функции управления
 * -------------------------------------------------------------------------- */

DW1000_Status DW1000_SetTxPower(DW1000_Device* dev, uint8_t power_level)
{
    uint32_t tx_power_reg = 0;
    /* Упрощённо: повторяем один и тот же уровень во все поля */
    tx_power_reg = (power_level << 0) | (power_level << 8) |
                   (power_level << 16) | (power_level << 24);
    return DW1000_WriteRegister32(dev, DW1000_TX_POWER, 0, tx_power_reg);
}

DW1000_Status DW1000_SetPhyConfig(DW1000_Device* dev, uint8_t channel,
                                    uint8_t data_rate, uint16_t preamble_len,
                                    uint8_t preamble_code, uint8_t prf, uint8_t pac_size)
{
    /* TODO: Полная настройка PHY */
    dev->channel = channel;
    dev->data_rate = data_rate;
    dev->preamble_len = preamble_len;
    dev->prf = prf;
    return DW1000_OK;
}

DW1000_Status DW1000_SendFrame(DW1000_Device* dev, const uint8_t* data, uint16_t len)
{
    DW1000_Status status;
    status = DW1000_WriteRegister(dev, DW1000_TX_BUFFER, 0, data, len);
    if (status != DW1000_OK) return status;

    uint32_t tx_fctrl = len; // TFLEN
    // TODO: установить биты TFLE, TXBR, TXPSR, PE в зависимости от конфигурации
    status = DW1000_WriteRegister32(dev, DW1000_TX_FCTRL, 0, tx_fctrl);
    if (status != DW1000_OK) return status;

    // Запуск передачи
    status = DW1000_WriteRegister8(dev, DW1000_SYS_CTRL, 0, 0x02); // TXSTRT
    return status;
}