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

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

#define RX_RING_BUFFER_SIZE 512

static struct {
    uint8_t buffer[RX_RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} rx_ring = {0};

static void ring_put(uint8_t data)
{
    uint16_t next = (rx_ring.head + 1) % RX_RING_BUFFER_SIZE;
    if (next != rx_ring.tail) {
        rx_ring.buffer[rx_ring.head] = data;
        rx_ring.head = next;
    }
}

static bool ring_get(uint8_t* data)
{
    if (rx_ring.head == rx_ring.tail) {
        return false;
    }
    *data = rx_ring.buffer[rx_ring.tail];
    rx_ring.tail = (rx_ring.tail + 1) % RX_RING_BUFFER_SIZE;
    return true;
}

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_CDC_IF
  * @brief Usb cdc interface
  * @{
  */

/* Private function prototypes -----------------------------------------------*/
static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* Global variables ----------------------------------------------------------*/

/* CDC Interface callback structure */
USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS
};

/* Private variables ----------------------------------------------------------*/
/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 0 */
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, NULL, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, NULL);
  return USBD_OK;
  /* USER CODE END 0 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 1 */
  return USBD_OK;
  /* USER CODE END 1 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 2 */
  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:
    case CDC_GET_ENCAPSULATED_RESPONSE:
    case CDC_SET_COMM_FEATURE:
    case CDC_GET_COMM_FEATURE:
    case CDC_CLEAR_COMM_FEATURE:
    case CDC_SET_LINE_CODING:
    case CDC_GET_LINE_CODING:
    case CDC_SET_CONTROL_LINE_STATE:
    case CDC_SEND_BREAK:
    default:
      break;
  }
  return USBD_OK;
  /* USER CODE END 2 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  * @param  pbuf: Pointer to data received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  for (uint32_t i = 0; i < *Len; i++) {
    ring_put(pbuf[i]);
  }
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &pbuf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return USBD_OK;
  /* USER CODE END 6 */
}

/* USER CODE BEGIN 7 */
uint8_t CDC_ReceiveByte(uint8_t* byte)
{
  return ring_get(byte) ? 1 : 0;
}

void CDC_Transmit(uint8_t* buf, uint16_t len)
{
  CDC_Transmit_FS(buf, len);
}
/* USER CODE END 7 */

/**
  * @}
  */

/**
  * @}
  */