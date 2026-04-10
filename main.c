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

/* main.c */
#include "debug_console.h"
#include "main.h"
#include "dw1000_driver.h"
#include "protocol.h"
#include "radio_defs.h"
#include "usbd_cdc_if.h"   // для CDC_ReceiveByte и CDC_Transmit

/* Глобальные объекты */
DW1000_Device tx_device;
DW1000_Device rx_device;

QueueHandle_t xRadioCommandQueue;
QueueHandle_t xRadioEventQueue;
QueueHandle_t xUSB_TxQueue;          // очередь из TxBuffer_t*

// глобальная переменная для очереди команд консоли
QueueHandle_t xConsoleCommandQueue;

SemaphoreHandle_t xTxCompleteSemaphore;
SemaphoreHandle_t xRxCompleteSemaphore;

volatile bool experiment_running = false;

/* Прототипы задач */
void USB_Command_Task(void* pvParameters);
void USB_Transmit_Task(void* pvParameters);
void Radio_Manager_Task(void* pvParameters);
void Periodic_TX_Task(void* pvParameters);
void Diagnostic_Stream_Task(void* pvParameters);

/* Конфигурация устройств */
void DW1000_ConfigureDevice1(DW1000_Device* dev) {
    dev->spi = &hspi1;
    dev->cs_port = GPIOA;
    dev->cs_pin = GPIO_PIN_4;
    dev->irq_port = GPIOB;
    dev->irq_pin = GPIO_PIN_0;
    dev->rst_port = GPIOC;
    dev->rst_pin = GPIO_PIN_0;
    dev->channel = 5;
    dev->data_rate = 2; // 6.8 Mbps
    dev->preamble_len = 128;
    dev->prf = 1; // 16 MHz
}

void DW1000_ConfigureDevice2(DW1000_Device* dev) {
    dev->spi = &hspi1;
    dev->cs_port = GPIOA;
    dev->cs_pin = GPIO_PIN_3;
    dev->irq_port = GPIOB;
    dev->irq_pin = GPIO_PIN_1;
    dev->rst_port = GPIOC;
    dev->rst_pin = GPIO_PIN_1;
    dev->channel = 5;
    dev->data_rate = 2;
    dev->preamble_len = 128;
    dev->prf = 1;
}

/* Функции управления скоростью SPI */
void spi_set_rate_low(void) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    HAL_SPI_Init(&hspi1);
}

void spi_set_rate_high(void) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    HAL_SPI_Init(&hspi1);
}

/* --------------------------------------------------------------------------
 * Задача управления радио (Radio_Manager_Task)
 * -------------------------------------------------------------------------- */
void Radio_Manager_Task(void* pvParameters) {
    RadioCommand_t* cmd;
    RadioEvent_t evt;
    DW1000_Status status;

    for (;;) {
        /* Ожидаем либо команду, либо событие */
        if (xQueueReceive(xRadioCommandQueue, &cmd, 0) == pdTRUE) {
            DW1000_Device* dev = (DW1000_Device*)cmd->dev;

            switch (cmd->cmd) {
                case RADIO_CMD_TX_FRAME:
                    status = DW1000_SendFrame(dev, cmd->params.tx_frame.data,
                                               cmd->params.tx_frame.len);
                    if (status != DW1000_OK) {
                        /* Отправить ответ об ошибке */
                    }
                    break;

                case RADIO_CMD_TX_PERIODIC:
                    /* Сохранить параметры и запустить периодическую передачу */
                    break;

                case RADIO_CMD_TX_STOP:
                    DW1000_WriteRegister8(dev, DW1000_SYS_CTRL, 0, 0x40); // TRXOFF
                    break;

                case RADIO_CMD_RX_START:
                    DW1000_WriteRegister16(dev, DW1000_SYS_CTRL, 0, 0x0100); // RXENAB
                    break;

                case RADIO_CMD_RX_STOP:
                    DW1000_WriteRegister8(dev, DW1000_SYS_CTRL, 0, 0x40);
                    break;

                case RADIO_CMD_SET_PHY:
                    DW1000_SetPhyConfig(dev,
                                        cmd->params.phy_config.channel,
                                        cmd->params.phy_config.data_rate,
                                        cmd->params.phy_config.preamble_len,
                                        cmd->params.phy_config.preamble_code,
                                        cmd->params.phy_config.prf,
                                        cmd->params.phy_config.pac_size);
                    break;

                case RADIO_CMD_SET_TX_POWER:
                    DW1000_SetTxPower(dev, cmd->params.tx_power.power_level);
                    break;

                case RADIO_CMD_TX_SWEEP:
                    /* Запуск режима свипирования */
                    break;

                case RADIO_CMD_DETECTOR_TEST:
                    /* Запуск теста детектора */
                    break;

                default:
                    break;
            }
            free(cmd);
        }

        /* Проверяем очередь событий от прерываний */
        if (xQueueReceive(xRadioEventQueue, &evt, 0) == pdTRUE) {
            DW1000_Device* dev = (DW1000_Device*)evt.dev;
            switch (evt.event) {
                case RADIO_EVT_TX_DONE:
                    /* Передача завершена, можно уведомить ожидающую задачу */
                    break;

                case RADIO_EVT_RX_DONE:
                    /* Принят пакет, можно извлечь данные и метрики */
                    if (experiment_running) {
                        uint8_t metrics[8];
                        /* ... заполнить метрики ... */
                        PROTOCOL_SendResponse(STATUS_OK, metrics, 8);
                    }
                    break;

                case RADIO_EVT_RX_TIMEOUT:
                case RADIO_EVT_RX_PHR_ERROR:
                case RADIO_EVT_RX_OVERRUN:
                case RADIO_EVT_FRAME_REJECTED:
                case RADIO_EVT_TX_BUFFER_ERROR:
                    /* Обработка ошибок */
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* --------------------------------------------------------------------------
 * Задачи USB
 * -------------------------------------------------------------------------- */
void USB_Command_Task(void* pvParameters) {
    uint8_t byte;
    while (1) {
        if (CDC_ReceiveByte(&byte)) {
            PROTOCOL_ProcessByte(byte);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void USB_Transmit_Task(void* pvParameters) {
    TxBuffer_t* tx_item;
    while (1) {
        if (xQueueReceive(xUSB_TxQueue, &tx_item, portMAX_DELAY) == pdPASS) {
            CDC_Transmit(tx_item->data, tx_item->len);
            free(tx_item->data);
            free(tx_item);
        }
    }
}

/* --------------------------------------------------------------------------
 * Вспомогательные задачи
 * -------------------------------------------------------------------------- */
void Periodic_TX_Task(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
        /* Если периодическая передача активна, сформировать команду и отправить в очередь */
    }
}

void Diagnostic_Stream_Task(void* pvParameters) {
    while (1) {
        if (experiment_running) {
            /* Собрать метрики и отправить */
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern UART_HandleTypeDef huart2;

/* --------------------------------------------------------------------------
 * main()
 * -------------------------------------------------------------------------- */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USB_DEVICE_Init();
    DEBUG_CONSOLE_Init(&huart2);

    DW1000_ConfigureDevice1(&tx_device);
    DW1000_ConfigureDevice2(&rx_device);

    /* Инициализация с пониженной скоростью SPI */
    spi_set_rate_low();

    if (DW1000_Init(&tx_device) != DW1000_OK) {
        Error_Handler();
    }
    if (DW1000_Init(&rx_device) != DW1000_OK) {
        Error_Handler();
    }

    spi_set_rate_high();

    PROTOCOL_Init();
    PROTOCOL_RegisterAllHandlers();

    /* Создание очередей и семафоров */
    xRadioCommandQueue = xQueueCreate(10, sizeof(RadioCommand_t*));
    xRadioEventQueue = xQueueCreate(10, sizeof(RadioEvent_t));
    xUSB_TxQueue = xQueueCreate(10, sizeof(TxBuffer_t*));
    xTxCompleteSemaphore = xSemaphoreCreateBinary();
    xRxCompleteSemaphore = xSemaphoreCreateBinary();

    /* Создание задач */
    xTaskCreate(USB_Command_Task, "USB_Cmd", 256, NULL, 2, NULL);
    xTaskCreate(USB_Transmit_Task, "USB_Tx", 256, NULL, 2, NULL);
    xTaskCreate(Radio_Manager_Task, "RadioMgr", 512, NULL, 3, NULL);
    xTaskCreate(Periodic_TX_Task, "PerTX", 256, NULL, 1, NULL);
    xTaskCreate(Diagnostic_Stream_Task, "Diag", 256, NULL, 1, NULL);
    xTaskCreate(DEBUG_Console_Task, "Console", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
}

/* Обработчик ошибок */
void Error_Handler(void) {
    while (1) {}
}