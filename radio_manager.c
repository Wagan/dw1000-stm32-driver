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

#include "debug_console.h"
#include "radio_manager.h"
#include "main.h"
#include "dw1000_driver.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <stdlib.h>

/* Внешние глобальные переменные драйвера (определены в main) */
extern DW1000_Device tx_dev;
extern DW1000_Device rx_dev;

/* Очередь команд от обработчиков */
static QueueHandle_t xCommandQueue;
/* Очередь результатов (для передачи ответа обратно) */
static QueueHandle_t xResultQueue;
/* Семафор для уведомления о завершении операции (для синхронизации с прерыванием) */
static SemaphoreHandle_t xRadioEventSemaphore;

/* Текущее состояние радио (для кэша) */
static struct {
    bool tx_busy;
    bool rx_busy;
    uint8_t channel;
    uint8_t data_rate;
    uint16_t preamble_len;
    uint8_t prf;
} radio_state;

/* Инициализация */
void RADIO_Manager_Init(void) {
    xCommandQueue = xQueueCreate(10, sizeof(RadioCommand));
    xResultQueue = xQueueCreate(10, sizeof(RadioResult));
    xRadioEventSemaphore = xSemaphoreCreateBinary();
    
    memset(&radio_state, 0, sizeof(radio_state));
}

/* Отправка команды и ожидание результата */
uint8_t RADIO_SendCommandAndWait(RadioCommand* cmd, RadioResult* result, uint32_t timeout_ms) {
    if (xQueueSend(xCommandQueue, cmd, portMAX_DELAY) != pdPASS) {
        return 0xFF; // ошибка очереди
    }
    // Ожидаем результат
    if (xQueueReceive(xResultQueue, result, pdMS_TO_TICKS(timeout_ms)) == pdPASS) {
        return result->status;
    } else {
        return STATUS_TIMEOUT; // таймаут
    }
}

/* Вспомогательные функции для работы с драйвером */

static uint8_t convert_data_rate(uint8_t api_rate) {
    switch (api_rate) {
        case 0: return 0; // 110 kbps
        case 1: return 1; // 850 kbps
        case 2: return 2; // 6.8 Mbps
        default: return 2;
    }
}

static uint8_t convert_prf(uint8_t api_prf) {
    return (api_prf == 0) ? 1 : 2; // 16 MHz -> 01, 64 MHz -> 10 в регистре CHAN_CTRL
}

/* Обработчик команды INIT */
static uint8_t handle_init(RadioCommand* cmd) {
    DW1000_Status status;
    status = DW1000_Init(&tx_dev);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    status = DW1000_Init(&rx_dev);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    
    // Установка параметров по умолчанию
    // TODO: установить безопасные значения
    
    radio_state.tx_busy = false;
    radio_state.rx_busy = false;
    radio_state.channel = 5;
    radio_state.data_rate = 2; // 6.8 Mbps
    radio_state.preamble_len = 128;
    radio_state.prf = 1; // 16 MHz
    
    return STATUS_OK;
}

/* Обработчик RESET_RADIO */
static uint8_t handle_reset(RadioCommand* cmd) {
    DW1000_SoftReset(&tx_dev);
    DW1000_SoftReset(&rx_dev);
    // После сброса нужно переинициализировать? Лучше вызвать INIT повторно
    handle_init(cmd);
    return STATUS_OK;
}

/* Обработчик SET_PHY_CONFIG */
static uint8_t handle_set_phy(RadioCommand* cmd, RadioResult* result) {
    DW1000_Status status;
    uint32_t chan_ctrl = 0;
    
    uint8_t channel = cmd->params.phy_config.channel;
    uint8_t data_rate = cmd->params.phy_config.data_rate;
    uint16_t preamble_len = cmd->params.phy_config.preamble_len;
    uint8_t preamble_code = cmd->params.phy_config.preamble_code;
    uint8_t prf = cmd->params.phy_config.prf;
    uint8_t pac_size = cmd->params.phy_config.pac_size;
    
    // Валидация (упрощённо), подумать о дополнении проверки
    if (channel < 1 || channel > 7) return STATUS_INVALID_PARAM;
    
    // Конфигурация канала
    chan_ctrl |= (channel & 0x0F); // TX_CHAN
    chan_ctrl |= ((channel & 0x0F) << 4); // RX_CHAN
    // PRF
    if (prf == 0) chan_ctrl |= (1 << 18); // 16 MHz
    else chan_ctrl |= (2 << 18); // 64 MHz
    // preamble code
    chan_ctrl |= ((preamble_code & 0x1F) << 22); // TX_PCODE
    chan_ctrl |= ((preamble_code & 0x1F) << 27); // RX_PCODE
    
    // Запись в оба модуля
    status = DW1000_WriteRegister32(&tx_dev, DW1000_CHAN_CTRL, 0, chan_ctrl);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    status = DW1000_WriteRegister32(&rx_dev, DW1000_CHAN_CTRL, 0, chan_ctrl);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    
    // Настройка TX_FCTRL (длина преамбулы, скорость)
    uint32_t tx_fctrl = 0;
    // PE и TXPSR для длины преамбулы (таблица 16)
    // Упрощённо: для 128 -> TXPSR=01, PE=01? Согласно таблице нужно вычислять
    if (preamble_len == 128) {
        tx_fctrl |= (1 << 18) | (1 << 20); // TXPSR=01, PE=01
    } else if (preamble_len == 64) {
        tx_fctrl |= (0 << 18) | (0 << 20);
    } // и т.д.
    
    // TXBR
    tx_fctrl |= (convert_data_rate(data_rate) << 13);
    
    status = DW1000_WriteRegister32(&tx_dev, DW1000_TX_FCTRL, 0, tx_fctrl);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    
    // Настройка приёмника: AGC_TUNE1, DRX_TUNE и т.д. (опущено для краткости)
    
    // Обновляем кэш
    radio_state.channel = channel;
    radio_state.data_rate = data_rate;
    radio_state.preamble_len = preamble_len;
    radio_state.prf = prf;
    
    return STATUS_OK;
}

/* Обработчик SET_TX_POWER */
static uint8_t handle_set_tx_power(RadioCommand* cmd) {
    uint8_t power = cmd->params.tx_power.power_level;
    // Запись в TX_POWER (просто пример, нужно отобразить power_level на значение регистра)
    uint32_t tx_power_val = 0x48484848; // среднее значение
    DW1000_WriteRegister32(&tx_dev, 0x1E, 0, tx_power_val);
    return STATUS_OK;
}

/* Обработчик TX_FRAME */
static uint8_t handle_tx_frame(RadioCommand* cmd, RadioResult* result) {
    if (radio_state.tx_busy) return STATUS_RADIO_BUSY;
    
    // Запись данных в TX_BUFFER
    uint16_t len = cmd->params.tx_frame.length;
    const uint8_t* payload = cmd->params.tx_frame.payload;
    
    // Проверка длины
    if (len > 1024) return STATUS_INVALID_PARAM;
    
    // Запись в буфер (используем WriteRegister с sub_addr = 0)
    DW1000_Status status = DW1000_WriteRegister(&tx_dev, DW1000_TX_BUFFER, 0, payload, len);
    if (status != DW1000_OK) return STATUS_RADIO_ERROR;
    
    // Установка длины в TX_FCTRL
    uint32_t tx_fctrl;
    DW1000_ReadRegister32(&tx_dev, DW1000_TX_FCTRL, 0, &tx_fctrl);
    tx_fctrl = (tx_fctrl & ~0x7F) | (len & 0x7F); // TFLEN
    tx_fctrl = (tx_fctrl & ~(0x7 << 7)) | ((len >> 7) << 7); // TFLE
    DW1000_WriteRegister32(&tx_dev, DW1000_TX_FCTRL, 0, tx_fctrl);
    
    // Очистка статуса перед стартом
    DW1000_WriteRegister32(&tx_dev, DW1000_SYS_STATUS, 0, 0x0080); // TXFRS
    
    // Запуск передачи
    DW1000_WriteRegister8(&tx_dev, DW1000_SYS_CTRL, 0, 0x02); // TXSTRT
    
    radio_state.tx_busy = true;
    
    // Ожидание завершения передачи (прерывание установит семафор)
    if (xSemaphoreTake(xRadioEventSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Проверить статус на ошибки
        uint32_t sys_status;
        DW1000_ReadRegister32(&tx_dev, DW1000_SYS_STATUS, 0, &sys_status);
        if (sys_status & 0x10000000) { // TXBERR
            radio_state.tx_busy = false;
            return STATUS_RADIO_ERROR;
        }
        radio_state.tx_busy = false;
        return STATUS_OK;
    } else {
        // Таймаут
        radio_state.tx_busy = false;
        DW1000_WriteRegister8(&tx_dev, DW1000_SYS_CTRL, 0, 0x40); // TRXOFF
        return STATUS_TIMEOUT;
    }
}

/* Обработчик GET_SIGNAL_METRICS */
static uint8_t handle_get_metrics(RadioCommand* cmd, RadioResult* result) {
    // Чтение из rx_dev
    uint32_t rx_fqual, rx_finfo;
    DW1000_ReadRegister32(&rx_dev, DW1000_RX_FQUAL, 0, &rx_fqual);
    DW1000_ReadRegister32(&rx_dev, DW1000_RX_FINFO, 0, &rx_finfo);
    
    // Извлечение полей (нужно уточнить смещения)
    uint16_t std_noise = (rx_fqual >> 0) & 0xFFFF;
    uint16_t fp_ampl2 = (rx_fqual >> 16) & 0xFFFF;
    uint16_t cir_pwr = (rx_fqual >> 0) & 0xFFFF; // на самом деле из другого слова
    
    uint16_t rxpacc = (rx_finfo >> 20) & 0xFFF;
    
    // Вычисление RSSI и SNR (упрощённо)
    result->result.metrics.rssi = cir_pwr; // placeholder
    result->result.metrics.snr = 100; // placeholder
    result->result.metrics.rxpacc = rxpacc;
    result->result.metrics.fp_index = 0; // нужно из другого регистра
    
    result->result_len = sizeof(result->result.metrics);
    return STATUS_OK;
}

/* Основная задача radio manager */
void RADIO_Manager_Task(void* pvParameters) {
    RadioCommand cmd;
    RadioResult result;
    
    for (;;) {
        if (xQueueReceive(xCommandQueue, &cmd, portMAX_DELAY) == pdPASS) {
            memset(&result, 0, sizeof(result));
            result.cmd_id = cmd.cmd_id;
            result.status = STATUS_OK;
            
            switch (cmd.cmd_id) {
                case RADIO_CMD_INIT:
                    result.status = handle_init(&cmd);
                    break;
                case RADIO_CMD_RESET:
                    result.status = handle_reset(&cmd);
                    break;
                case RADIO_CMD_SET_PHY:
                    result.status = handle_set_phy(&cmd, &result);
                    break;
                case RADIO_CMD_SET_TX_POWER:
                    result.status = handle_set_tx_power(&cmd);
                    break;
                case RADIO_CMD_TX_FRAME:
                    result.status = handle_tx_frame(&cmd, &result);
                    break;
                case RADIO_CMD_GET_METRICS:
                    result.status = handle_get_metrics(&cmd, &result);
                    break;
                // ... другие команды
                default:
                    result.status = STATUS_INTERNAL_ERROR;
                    break;
            }
            
            // Отправляем результат обратно в очередь
            xQueueSend(xResultQueue, &result, 0);
        }
    }
}

/* Обработчик прерывания от DW1000 (должен вызываться из внешнего прерывания) */
void RADIO_IRQHandler(DW1000_Device* dev) {
    // Уведомляем задачу radio manager, что произошло событие
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xRadioEventSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}