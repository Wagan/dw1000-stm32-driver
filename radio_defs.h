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

#ifndef RADIO_DEFS_H
#define RADIO_DEFS_H

#include <stdint.h>

/* Типы команд для очереди xRadioCommandQueue */
typedef enum {
    RADIO_CMD_TX_FRAME,
    RADIO_CMD_TX_PERIODIC,
    RADIO_CMD_TX_STOP,
    RADIO_CMD_RX_START,
    RADIO_CMD_RX_STOP,
    RADIO_CMD_SET_PHY,
    RADIO_CMD_SET_TX_POWER,
    RADIO_CMD_TX_SWEEP,
    RADIO_CMD_DETECTOR_TEST
} RadioCommandType;

/* Структура команды (передаётся по очереди) */
typedef struct {
    RadioCommandType cmd;
    void* dev;                    /* указатель на DW1000_Device */
    union {
        struct {
            uint16_t len;
            uint8_t data[256];    /* payload */
        } tx_frame;
        struct {
            uint16_t period_ms;
            uint16_t len;
            uint8_t data[256];
        } tx_periodic;
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
            uint8_t channel_start;
            uint8_t channel_end;
            uint8_t power_start;
            uint8_t power_end;
            uint16_t preamble_len;
        } tx_sweep;
        struct {
            uint16_t num_packets;
            uint8_t power_start;
            uint8_t power_end;
        } detector_test;
    } params;
} RadioCommand_t;

/* Типы событий для очереди xRadioEventQueue */
typedef enum {
    RADIO_EVT_RX_DONE,
    RADIO_EVT_RX_TIMEOUT,
    RADIO_EVT_RX_PHR_ERROR,
    RADIO_EVT_RX_OVERRUN,
    RADIO_EVT_FRAME_REJECTED,
    RADIO_EVT_TX_DONE,
    RADIO_EVT_TX_BUFFER_ERROR
} RadioEventType;

/* Структура события */
typedef struct {
    void* dev;                    /* указатель на DW1000_Device */
    RadioEventType event;
    union {
        struct {
            uint32_t timestamp;
            uint16_t rxpacc;
            int16_t fp_ampl1, fp_ampl2, fp_ampl3;
            uint16_t fp_index;
            uint16_t std_noise;
            uint16_t cir_pwr;
        } rx_done;
        /* можно расширить для других событий */
    } info;
} RadioEvent_t;

#endif /* RADIO_DEFS_H */
