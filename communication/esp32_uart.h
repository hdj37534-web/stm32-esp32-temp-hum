#ifndef ESP32_UART_H
#define ESP32_UART_H

#include "stm32f10x.h"

#define ESP32_UART_FRAME_HEAD0           0xAAU
#define ESP32_UART_FRAME_HEAD1           0x55U
#define ESP32_UART_FRAME_TYPE_STATUS     0x01U
#define ESP32_UART_FRAME_TYPE_CONTROL    0x81U

typedef struct
{
    uint8_t alarmOn;
    uint8_t ledOn;
} Esp32UartControl;

void Esp32Uart_Init(void);
void Esp32Uart_Task(void);
void Esp32Uart_SendStatus(uint8_t temperature, uint8_t humidity,
                          uint8_t alarmOn, uint8_t ledOn);
uint8_t Esp32Uart_ReadControl(Esp32UartControl *control);

#endif
