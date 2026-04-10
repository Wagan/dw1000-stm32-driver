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
#include "main.h"
#include "dw1000_driver.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Внешние переменные из main.c */
extern DW1000_Device tx_device;
extern DW1000_Device rx_device;
extern volatile bool experiment_running;

/* Локальные переменные */
static UART_HandleTypeDef* console_uart = NULL;
static DebugLevel current_level = DEBUG_LEVEL_INFO;
static char print_buffer[256];

/* Очередь для команд из консоли (опционально) */
QueueHandle_t xConsoleCommandQueue;

/* Таблица команд консоли */
typedef struct {
    const char* name;
    const char* help;
    void (*handler)(int argc, char** argv);
} ConsoleCommand;

/* Прототипы обработчиков команд */
static void cmd_help(int argc, char** argv);
static void cmd_status(int argc, char** argv);
static void cmd_tx(int argc, char** argv);
static void cmd_rx(int argc, char** argv);
static void cmd_power(int argc, char** argv);
static void cmd_channel(int argc, char** argv);
static void cmd_reset(int argc, char** argv);
static void cmd_log(int argc, char** argv);
static void cmd_cir(int argc, char** argv);
static void cmd_metrics(int argc, char** argv);
static void cmd_experiment(int argc, char** argv);

/* Список команд */
static const ConsoleCommand commands[] = {
    {"help", "Показать список команд", cmd_help},
    {"h", "Алиас для help", cmd_help},
    {"status", "Показать состояние системы", cmd_status},
    {"tx", "Передать кадр: tx <длина> <данные в hex>", cmd_tx},
    {"rx", "Включить/выключить приёмник: rx on/off", cmd_rx},
    {"power", "Установить мощность: power <tx/rx> <0-31>", cmd_power},
    {"channel", "Установить канал: channel <1-7>", cmd_channel},
    {"reset", "Сброс радио: reset <tx/rx/all>", cmd_reset},
    {"log", "Уровень отладки: log <error/warning/info/debug/verbose>", cmd_log},
    {"cir", "Прочитать CIR: cir <offset> <length>", cmd_cir},
    {"metrics", "Показать метрики последнего пакета", cmd_metrics},
    {"exp", "Эксперимент: exp start/stop", cmd_experiment},
    {NULL, NULL, NULL}
};

/* --------------------------------------------------------------------------
 * Инициализация
 * -------------------------------------------------------------------------- */
void DEBUG_CONSOLE_Init(UART_HandleTypeDef* huart)
{
    console_uart = huart;
    xConsoleCommandQueue = xQueueCreate(10, sizeof(char[64])); // очередь строк команд
}

void DEBUG_SetUART(UART_HandleTypeDef* huart)
{
    console_uart = huart;
}

void DEBUG_SetLevel(DebugLevel level)
{
    current_level = level;
}

/* --------------------------------------------------------------------------
 * Вывод в UART (блокирующий)
 * -------------------------------------------------------------------------- */
static void uart_send(const char* str)
{
    if (console_uart != NULL) {
        HAL_UART_Transmit(console_uart, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
    }
}

void DEBUG_Print(const char* str)
{
    uart_send(str);
}

void DEBUG_Println(const char* str)
{
    uart_send(str);
    uart_send("\r\n");
}

void DEBUG_Printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(print_buffer, sizeof(print_buffer), format, args);
    va_end(args);
    uart_send(print_buffer);
}

void DEBUG_PrintHex(const uint8_t* data, uint16_t len)
{
    char hex[5];
    for (uint16_t i = 0; i < len; i++) {
        snprintf(hex, sizeof(hex), "%02X ", data[i]);
        uart_send(hex);
    }
    uart_send("\r\n");
}

void DEBUG_PrintStatus(void)
{
    DEBUG_Printf("\r\n=== Состояние МКС ===\r\n");
    DEBUG_Printf("TX: channel=%d, rate=%d, preamble=%d, PRF=%d\r\n",
                 tx_device.channel, tx_device.data_rate,
                 tx_device.preamble_len, tx_device.prf);
    DEBUG_Printf("RX: channel=%d, rate=%d, preamble=%d, PRF=%d\r\n",
                 rx_device.channel, rx_device.data_rate,
                 rx_device.preamble_len, rx_device.prf);
    DEBUG_Printf("Experiment running: %s\r\n", experiment_running ? "YES" : "NO");
}

/* --------------------------------------------------------------------------
 * Парсер команд
 * -------------------------------------------------------------------------- */
static void parse_and_execute(char* cmd_line)
{
    char* argv[10];
    int argc = 0;
    char* token = strtok(cmd_line, " \t\r\n");
    
    while (token != NULL && argc < 10) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\r\n");
    }
    
    if (argc == 0) return;
    
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }
    
    DEBUG_Println("Неизвестная команда. Введите 'help' для списка.");
}

/* --------------------------------------------------------------------------
 * Задача консоли
 * -------------------------------------------------------------------------- */
void DEBUG_Console_Task(void* pvParameters)
{
    char rx_char;
    char cmd_line[64];
    int idx = 0;
    
    DEBUG_Println("\r\n=== МКС Debug Console v1.3.01 ===\r\n");
    DEBUG_Print("> ");
    
    while (1) {
        if (console_uart && HAL_UART_Receive(console_uart, (uint8_t*)&rx_char, 1, pdMS_TO_TICKS(100)) == HAL_OK) {
            if (rx_char == '\r' || rx_char == '\n') {
                DEBUG_Print("\r\n");
                if (idx > 0) {
                    cmd_line[idx] = '\0';
                    parse_and_execute(cmd_line);
                    idx = 0;
                }
                DEBUG_Print("> ");
            } else if (rx_char == '\b' || rx_char == 127) { // backspace
                if (idx > 0) {
                    idx--;
                    DEBUG_Print("\b \b");
                }
            } else if (idx < (int)sizeof(cmd_line) - 1) {
                cmd_line[idx++] = rx_char;
                DEBUG_Print(&rx_char); // эхо
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Обработчики команд
 * -------------------------------------------------------------------------- */
static void cmd_help(int argc, char** argv)
{
    (void)argc; (void)argv;
    DEBUG_Println("\nДоступные команды:");
    for (int i = 0; commands[i].name != NULL; i++) {
        if (commands[i].name[0] != 'h') { // не показываем алиасы отдельно
            DEBUG_Printf("  %-12s - %s\r\n", commands[i].name, commands[i].help);
        }
    }
}

static void cmd_status(int argc, char** argv)
{
    (void)argc; (void)argv;
    DEBUG_PrintStatus();
}

static void cmd_tx(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Использование: tx <длина> <данные в hex>");
        return;
    }
    
    uint16_t len = atoi(argv[1]);
    uint8_t data[256];
    
    if (argc >= 3) {
        // парсим hex-строку
        char* hex = argv[2];
        uint16_t hex_len = strlen(hex);
        for (uint16_t i = 0; i < len && i*2 < hex_len; i++) {
            char byte_str[3] = {hex[i*2], hex[i*2+1], 0};
            data[i] = (uint8_t)strtol(byte_str, NULL, 16);
        }
    } else {
        // тестовые данные
        for (uint16_t i = 0; i < len; i++) data[i] = i & 0xFF;
    }
    
    DEBUG_Printf("Передача %d байт... ", len);
    
    // отправка через существующую очередь команд
    RadioCommand_t* cmd = malloc(sizeof(RadioCommand_t) + len);
    if (cmd) {
        cmd->cmd = RADIO_CMD_TX_FRAME;
        cmd->dev = &tx_device;
        cmd->params.tx_frame.len = len;
        memcpy(cmd->params.tx_frame.data, data, len);
        
        if (xQueueSend(xRadioCommandQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
            DEBUG_Println("OK");
        } else {
            DEBUG_Println("Ошибка очереди");
            free(cmd);
        }
    }
}

static void cmd_rx(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Использование: rx on/off");
        return;
    }
    
    RadioCommand_t* cmd = malloc(sizeof(RadioCommand_t));
    if (!cmd) {
        DEBUG_Println("Ошибка памяти");
        return;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        cmd->cmd = RADIO_CMD_RX_START;
        cmd->dev = &rx_device;
        DEBUG_Println("Включение приёмника...");
    } else if (strcmp(argv[1], "off") == 0) {
        cmd->cmd = RADIO_CMD_RX_STOP;
        cmd->dev = &rx_device;
        DEBUG_Println("Выключение приёмника...");
    } else {
        DEBUG_Println("Использование: rx on/off");
        free(cmd);
        return;
    }
    
    xQueueSend(xRadioCommandQueue, &cmd, pdMS_TO_TICKS(100));
}

static void cmd_power(int argc, char** argv)
{
    if (argc < 3) {
        DEBUG_Println("Использование: power <tx/rx> <0-31>");
        return;
    }
    
    uint8_t power = atoi(argv[2]);
    if (power > 31) {
        DEBUG_Println("Мощность должна быть 0-31");
        return;
    }
    
    DW1000_Device* dev = NULL;
    if (strcmp(argv[1], "tx") == 0) dev = &tx_device;
    else if (strcmp(argv[1], "rx") == 0) dev = &rx_device;
    else {
        DEBUG_Println("Укажите tx или rx");
        return;
    }
    
    DW1000_SetTxPower(dev, power);
    DEBUG_Printf("Мощность %s установлена: %d\r\n", argv[1], power);
}

static void cmd_channel(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Использование: channel <1-7>");
        return;
    }
    
    uint8_t ch = atoi(argv[1]);
    if (ch < 1 || ch > 7 || ch == 6) {
        DEBUG_Println("Канал должен быть 1-5 или 7");
        return;
    }
    
    // Здесь должна быть настройка через драйвер
    tx_device.channel = ch;
    rx_device.channel = ch;
    DEBUG_Printf("Канал установлен: %d\r\n", ch);
}

static void cmd_reset(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Использование: reset <tx/rx/all>");
        return;
    }
    
    if (strcmp(argv[1], "tx") == 0 || strcmp(argv[1], "all") == 0) {
        DW1000_SoftReset(&tx_device);
        DEBUG_Println("TX сброшен");
    }
    if (strcmp(argv[1], "rx") == 0 || strcmp(argv[1], "all") == 0) {
        DW1000_SoftReset(&rx_device);
        DEBUG_Println("RX сброшен");
    }
}

static void cmd_log(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Текущий уровень: info");
        DEBUG_Println("Использование: log <error/warning/info/debug/verbose>");
        return;
    }
    
    DebugLevel new_level = DEBUG_LEVEL_INFO;
    if (strcmp(argv[1], "error") == 0) new_level = DEBUG_LEVEL_ERROR;
    else if (strcmp(argv[1], "warning") == 0) new_level = DEBUG_LEVEL_WARNING;
    else if (strcmp(argv[1], "info") == 0) new_level = DEBUG_LEVEL_INFO;
    else if (strcmp(argv[1], "debug") == 0) new_level = DEBUG_LEVEL_DEBUG;
    else if (strcmp(argv[1], "verbose") == 0) new_level = DEBUG_LEVEL_VERBOSE;
    else {
        DEBUG_Println("Неизвестный уровень");
        return;
    }
    
    DEBUG_SetLevel(new_level);
    DEBUG_Printf("Уровень отладки установлен: %s\r\n", argv[1]);
}

static void cmd_cir(int argc, char** argv)
{
    if (argc < 3) {
        DEBUG_Println("Использование: cir <offset> <length>");
        return;
    }
    
    uint16_t offset = atoi(argv[1]);
    uint16_t length = atoi(argv[2]);
    
    if (length > 32) length = 32; // ограничим вывод
    
    uint8_t cir_data[128];
    DW1000_ReadRegister(&rx_device, DW1000_ACC_MEM, offset * 4, cir_data, length * 4);
    
    DEBUG_Printf("CIR offset=%d, length=%d:\r\n", offset, length);
    for (uint16_t i = 0; i < length; i++) {
        int16_t i_val = (int16_t)(cir_data[i*4] | (cir_data[i*4+1] << 8));
        int16_t q_val = (int16_t)(cir_data[i*4+2] | (cir_data[i*4+3] << 8));
        DEBUG_Printf("[%3d] I=%6d Q=%6d\r\n", i, i_val, q_val);
    }
}

static void cmd_metrics(int argc, char** argv)
{
    (void)argc; (void)argv;
    
    uint32_t reg;
    DW1000_ReadRegister32(&rx_device, DW1000_RX_FINFO, 0, &reg);
    uint16_t rxpacc = (reg >> 20) & 0xFFF;
    
    DW1000_ReadRegister32(&rx_device, DW1000_RX_FQUAL, 0, &reg);
    uint16_t std_noise = reg & 0xFFFF;
    
    DW1000_ReadRegister(&rx_device, DW1000_RX_TIME, 4, (uint8_t*)&reg, 4);
    uint16_t fp_index = reg & 0xFFFF;
    int16_t fp_ampl1 = (reg >> 16) & 0xFFFF;
    
    DEBUG_Printf("Метрики последнего пакета:\r\n");
    DEBUG_Printf("  RXPACC:    %d\r\n", rxpacc);
    DEBUG_Printf("  FP_INDEX:  %d\r\n", fp_index);
    DEBUG_Printf("  FP_AMPL1:  %d\r\n", fp_ampl1);
    DEBUG_Printf("  STD_NOISE: %d\r\n", std_noise);
}

static void cmd_experiment(int argc, char** argv)
{
    if (argc < 2) {
        DEBUG_Println("Использование: exp start/stop");
        return;
    }
    
    if (strcmp(argv[1], "start") == 0) {
        experiment_running = true;
        DEBUG_Println("Эксперимент запущен");
    } else if (strcmp(argv[1], "stop") == 0) {
        experiment_running = false;
        DEBUG_Println("Эксперимент остановлен");
    } else {
        DEBUG_Println("Использование: exp start/stop");
    }
}