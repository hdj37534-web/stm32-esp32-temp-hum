#include "esp32_uart.h"
#include "bsp_usart.h"
#include <stddef.h>

#define ESP32_UART_MAX_PAYLOAD_LEN       16U
#define ESP32_UART_CONTROL_PAYLOAD_LEN   1U
#define ESP32_UART_STATUS_PAYLOAD_LEN    3U

typedef enum
{
    ESP32_UART_RX_HEAD0 = 0,
    ESP32_UART_RX_HEAD1,
    ESP32_UART_RX_TYPE,
    ESP32_UART_RX_LENGTH,
    ESP32_UART_RX_PAYLOAD,
    ESP32_UART_RX_CHECKSUM
} Esp32UartRxState;

static volatile Esp32UartRxState s_rxState;
static volatile uint8_t s_rxType;
static volatile uint8_t s_rxLength;
static volatile uint8_t s_rxIndex;
static volatile uint8_t s_rxChecksum;
static volatile uint8_t s_rxPayload[ESP32_UART_MAX_PAYLOAD_LEN];
static volatile Esp32UartControl s_control;
static volatile uint8_t s_controlPending;

static void Esp32Uart_ResetRx(void)
{
    s_rxState = ESP32_UART_RX_HEAD0;
    s_rxLength = 0U;
    s_rxIndex = 0U;
    s_rxChecksum = 0U;
}

static void Esp32Uart_HandleFrame(void)
{
    if ((s_rxType == ESP32_UART_FRAME_TYPE_CONTROL) &&
        (s_rxLength == ESP32_UART_CONTROL_PAYLOAD_LEN))
    {
        s_control.alarmOn = s_rxPayload[0] & 0x01U;
        s_control.ledOn = (s_rxPayload[0] >> 1U) & 0x01U;
        s_controlPending = 1U;
    }
}

static void Esp32Uart_RxByte(uint8_t data)
{
    switch (s_rxState)
    {
        case ESP32_UART_RX_HEAD0:
            if (data == ESP32_UART_FRAME_HEAD0)
            {
                s_rxState = ESP32_UART_RX_HEAD1;
            }
            break;

        case ESP32_UART_RX_HEAD1:
            if (data == ESP32_UART_FRAME_HEAD1)
            {
                s_rxState = ESP32_UART_RX_TYPE;
            }
            else if (data != ESP32_UART_FRAME_HEAD0)
            {
                Esp32Uart_ResetRx();
            }
            break;

        case ESP32_UART_RX_TYPE:
            s_rxType = data;
            s_rxChecksum = data;
            s_rxState = ESP32_UART_RX_LENGTH;
            break;

        case ESP32_UART_RX_LENGTH:
            if (data > ESP32_UART_MAX_PAYLOAD_LEN)
            {
                Esp32Uart_ResetRx();
                break;
            }
            s_rxLength = data;
            s_rxIndex = 0U;
            s_rxChecksum ^= data;
            s_rxState = (data == 0U) ? ESP32_UART_RX_CHECKSUM : ESP32_UART_RX_PAYLOAD;
            break;

        case ESP32_UART_RX_PAYLOAD:
            s_rxPayload[s_rxIndex++] = data;
            s_rxChecksum ^= data;
            if (s_rxIndex >= s_rxLength)
            {
                s_rxState = ESP32_UART_RX_CHECKSUM;
            }
            break;

        case ESP32_UART_RX_CHECKSUM:
            if (data == s_rxChecksum)
            {
                Esp32Uart_HandleFrame();
            }
            Esp32Uart_ResetRx();
            break;

        default:
            Esp32Uart_ResetRx();
            break;
    }
}

static void Esp32Uart_SendFrame(uint8_t type, const uint8_t *payload, uint8_t length)
{
    uint8_t frame[ESP32_UART_MAX_PAYLOAD_LEN + 5U];
    uint8_t index;
    uint8_t checksum;

    if (length > ESP32_UART_MAX_PAYLOAD_LEN)
    {
        return;
    }

    frame[0] = ESP32_UART_FRAME_HEAD0;
    frame[1] = ESP32_UART_FRAME_HEAD1;
    frame[2] = type;
    frame[3] = length;
    checksum = type ^ length;

    for (index = 0U; index < length; index++)
    {
        frame[index + 4U] = payload[index];
        checksum ^= payload[index];
    }

    frame[length + 4U] = checksum;
    Usart_SendString(USART2, frame, (uint16_t)length + 5U);
}

void Esp32Uart_Init(void)
{
    Esp32Uart_ResetRx();
    s_control.alarmOn = 0U;
    s_control.ledOn = 0U;
    s_controlPending = 0U;
    Usart2_Init(115200U);
}

void Esp32Uart_Task(void)
{
    /* The receive FSM runs in USART2_IRQHandler; foreground work is event-driven. */
}

void Esp32Uart_SendStatus(uint8_t temperature, uint8_t humidity,
                          uint8_t alarmOn, uint8_t ledOn)
{
    uint8_t payload[ESP32_UART_STATUS_PAYLOAD_LEN];

    payload[0] = temperature;
    payload[1] = humidity;
    payload[2] = (alarmOn & 0x01U) | ((ledOn & 0x01U) << 1U);
    Esp32Uart_SendFrame(ESP32_UART_FRAME_TYPE_STATUS, payload, sizeof(payload));
}

uint8_t Esp32Uart_ReadControl(Esp32UartControl *control)
{
    if ((control == NULL) || (s_controlPending == 0U))
    {
        return 0U;
    }

    control->alarmOn = s_control.alarmOn;
    control->ledOn = s_control.ledOn;
    s_controlPending = 0U;
    return 1U;
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        Esp32Uart_RxByte((uint8_t)USART_ReceiveData(USART2));
    }
}
