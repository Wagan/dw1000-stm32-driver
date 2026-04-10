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

#ifndef RADIO_MANAGER_H
#define RADIO_MANAGER_H

#include "dw1000_driver.h"
#include <stdint.h>
#include <stdbool.h>

/* Типы операций, которые может выполнить radio manager */
typedef enum {
    RADIO_CMD_NONE,
    RADIO_CMD_INIT,
    RADIO_CMD_RESET,
    RADIO_CMD_SET_PHY,
    RADIO_CMD_SET_TX_POWER,
    RADIO_CMD_TX_FRAME,
    RADIO_CMD_TX_PERIODIC_START,
    RADIO_CMD_TX_PERIODIC_STOP,
    RADIO_CMD_RX_START,
    RADIO_CMD_RX_STOP,
    RADIO_CMD_GET_METRICS,
    RADIO_CMD_GET_CIR,
    RADIO_CMD_START_EXPERIMENT,
    RADIO_CMD_STOP_EXPERIMENT,
    RADIO_CMD_TX_SWEEP,
    RADIO_CMD_DETECTOR_TEST
} RadioCommandID;

/* Параметры для команд */
typedef struct {
    RadioCommandID cmd_id;
    uint8_t target; // 0 - tx, 1 - rx, 2 - both (для некоторых)
    union {
        struct {
            uint8_t channel;
            uint8_t data_rate;
            uint16_t preamble_len;
            uint8_t preamble_code;
            uint8_t prf;
            uint8_t pac_size;
        } phy_config;
        struct {
            uint8_t power_level;
        } tx_power;
        struct {
            uint16_t length;
            uint8_t* payload; // указатель на данные (должны быть скопированы или переданы через буфер)
        } tx_frame;
        struct {
            uint16_t period_ms;
            uint16_t length;
            uint8_t* payload;
        } tx_periodic;
        struct {
            uint16_t offset;
            uint16_t length;
        } cir;
        struct {
            uint16_t num_packets;
            uint8_t power_start;
            uint8_t power_end;
        } detector_test;
        // и т.д.
    } params;
} RadioCommand;

/* Результат выполнения команды */
typedef struct {
    RadioCommandID cmd_id;
    uint8_t status; // 0 - успешно, иначе код ошибки
    union {
        struct {
            uint8_t tx_state;
            uint8_t rx_state;
            uint8_t channel;
            uint8_t data_rate;
            uint16_t preamble_len;
            uint8_t prf;
        } status_info;
        struct {
            uint16_t rssi;       // в 0.1 dBm или другом масштабе
            uint16_t snr;        // в 0.1 dB
            uint16_t rxpacc;
            uint16_t fp_index;
        } metrics;
        struct {
            uint16_t length;
            int16_t* iq_data;   // указатель на буфер с комплексными отсчётами
        } cir;
        // другие результаты
    } result;
    uint8_t result_len; // размер данных в result (для отправки)
} RadioResult;

/* Инициализация radio manager (создание очередей и задач) */
void RADIO_Manager_Init(void);

/* Отправка команды и ожидание результата (блокирующий вызов для обработчиков) */
uint8_t RADIO_SendCommandAndWait(RadioCommand* cmd, RadioResult* result, uint32_t timeout_ms);

/* Функция задачи radio manager (должна быть запущена как отдельная задача) */
void RADIO_Manager_Task(void* pvParameters);

#endif
