#include "app_dht_display.h"
#include "bsp_dht11.h"
#include "bsp_oled.h"
#include "bsp_key.h"
#include "bsp_Alarm.h"
#include "bsp_led.h"
#include "esp32_uart.h"

#define DHT11_SAMPLE_PERIOD_MS    2000U
#define TEMP_LIMIT_DEFAULT         35U
#define HUMI_LIMIT_DEFAULT         80U
#define TEMP_LIMIT_MAX             99U
#define HUMI_LIMIT_MAX             99U

#define MAIN_VALUE_X               48U
#define ALARM_VALUE_X              56U
#define LIMIT_VALUE_X              0U
#define LIMIT_VALUE_Y              2U
#define DISPLAY_VALUE_WIDTH        2U
#define ALARM_TEXT_WIDTH           3U

typedef enum
{
    APP_DHT_DISPLAY_STATE_WAIT = 0,
    APP_DHT_DISPLAY_STATE_SAMPLE,
    APP_DHT_DISPLAY_STATE_RENDER
} AppDhtDisplayState;

typedef enum
{
    APP_SCREEN_MAIN = 0,
    APP_SCREEN_TEMP_LIMIT,
    APP_SCREEN_HUMI_LIMIT
} AppScreen;

static volatile uint32_t s_tickMs;
static uint32_t s_nextSampleMs;
static DHT11_Data_TypeDef s_dht11Data;
static uint8_t s_sampleValid;
static uint8_t s_alarmActive;
static uint8_t s_remoteAlarmOn;
static uint8_t s_ledOn;
static uint8_t s_tempLimit;
static uint8_t s_humiLimit;
static AppDhtDisplayState s_state;
static AppScreen s_screen;

static uint8_t App_DhtDisplay_IsTimeReached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static void App_DhtDisplay_ShowText(uint8_t x, uint8_t y, const char *text)
{
    while (*text != '\0')
    {
        OLED_ShowChar(x, y, (uint8_t)*text);
        x += 8U;
        text++;
    }
}

static void App_DhtDisplay_ClearChars(uint8_t x, uint8_t y, uint8_t count)
{
    while (count > 0U)
    {
        OLED_ShowChar(x, y, ' ');
        x += 8U;
        count--;
    }
}

static void App_DhtDisplay_DrawScreenStatic(void)
{
    OLED_Clear();

    if (s_screen == APP_SCREEN_MAIN)
    {
        App_DhtDisplay_ShowText(0U, 0U, "Temp:");
        App_DhtDisplay_ShowText(72U, 0U, "C");
        App_DhtDisplay_ShowText(0U, 2U, "Humi:");
        App_DhtDisplay_ShowText(72U, 2U, "%");
        App_DhtDisplay_ShowText(0U, 4U, "Alarm:");
    }
    else if (s_screen == APP_SCREEN_TEMP_LIMIT)
    {
        App_DhtDisplay_ShowText(0U, 0U, "TEMP LIMIT");
        App_DhtDisplay_ShowText(24U, 2U, "C");
    }
    else
    {
        App_DhtDisplay_ShowText(0U, 0U, "HUMI LIMIT");
        App_DhtDisplay_ShowText(24U, 2U, "%");
    }
}

static void App_DhtDisplay_RenderLimitValue(void)
{
    uint8_t limit;

    if (s_screen == APP_SCREEN_MAIN)
    {
        return;
    }

    limit = (s_screen == APP_SCREEN_TEMP_LIMIT) ? s_tempLimit : s_humiLimit;
    App_DhtDisplay_ClearChars(LIMIT_VALUE_X, LIMIT_VALUE_Y, DISPLAY_VALUE_WIDTH);
    OLED_ShowNum(LIMIT_VALUE_X, LIMIT_VALUE_Y, limit, DISPLAY_VALUE_WIDTH, 1U);
}

static void App_DhtDisplay_Render(void)
{
    if (s_screen != APP_SCREEN_MAIN)
    {
        App_DhtDisplay_RenderLimitValue();
        return;
    }

    App_DhtDisplay_ClearChars(MAIN_VALUE_X, 0U, DISPLAY_VALUE_WIDTH);
    App_DhtDisplay_ClearChars(MAIN_VALUE_X, 2U, DISPLAY_VALUE_WIDTH);
    App_DhtDisplay_ClearChars(ALARM_VALUE_X, 4U, ALARM_TEXT_WIDTH);

    if (s_sampleValid != 0U)
    {
        OLED_ShowNum(MAIN_VALUE_X, 0U, s_dht11Data.temp_int, DISPLAY_VALUE_WIDTH, 1U);
        OLED_ShowNum(MAIN_VALUE_X, 2U, s_dht11Data.humi_int, DISPLAY_VALUE_WIDTH, 1U);
        App_DhtDisplay_ShowText(ALARM_VALUE_X, 4U, (s_alarmActive != 0U) ? "ON" : "OFF");
    }
    else
    {
        App_DhtDisplay_ShowText(MAIN_VALUE_X, 0U, "ER");
        App_DhtDisplay_ShowText(MAIN_VALUE_X, 2U, "ER");
        App_DhtDisplay_ShowText(ALARM_VALUE_X, 4U, "OFF");
    }
}

static void App_DhtDisplay_UpdateAlarm(void)
{
    if ((s_remoteAlarmOn != 0U) ||
        ((s_sampleValid != 0U) &&
         ((s_dht11Data.temp_int > s_tempLimit) ||
          (s_dht11Data.humi_int > s_humiLimit))))
    {
        s_alarmActive = 1U;
        Alarm_ON();
    }
    else
    {
        s_alarmActive = 0U;
        Alarm_OFF();
    }
}

static void App_DhtDisplay_ApplyLed(uint8_t ledOn)
{
    if (ledOn != 0U)
    {
        LED_ON();
    }
    else
    {
        LED_OFF();
    }
}

/* Send the state currently applied by STM32, never stale requested values. */
static void App_DhtDisplay_SendStatus(void)
{
    if (s_sampleValid != 0U)
    {
        Esp32Uart_SendStatus(s_dht11Data.temp_int, s_dht11Data.humi_int,
                             s_alarmActive, s_ledOn);
    }
}

static void App_DhtDisplay_ProcessEsp32(void)
{
    Esp32UartControl control;

    Esp32Uart_Task();
    if (Esp32Uart_ReadControl(&control) != 0U)
    {
        s_remoteAlarmOn = control.alarmOn;
        s_ledOn = control.ledOn;

        App_DhtDisplay_ApplyLed(s_ledOn);
        App_DhtDisplay_UpdateAlarm();
        App_DhtDisplay_SendStatus();
    }
}

static uint8_t App_DhtDisplay_AdjustLimit(uint8_t limit, uint8_t maximum, uint8_t key)
{
    if ((key == 2U) && (limit < maximum))
    {
        return limit + 1U;
    }
    if ((key == 3U) && (limit > 0U))
    {
        return limit - 1U;
    }

    return limit;
}

static void App_DhtDisplay_ProcessKey(void)
{
    uint8_t key = Key_Scan(0U);

    if (key == 1U)
    {
        s_screen = (AppScreen)((s_screen + 1U) % 3U);
        App_DhtDisplay_DrawScreenStatic();
        s_state = APP_DHT_DISPLAY_STATE_RENDER;
    }
    else if ((s_screen == APP_SCREEN_TEMP_LIMIT) && ((key == 2U) || (key == 3U)))
    {
        s_tempLimit = App_DhtDisplay_AdjustLimit(s_tempLimit, TEMP_LIMIT_MAX, key);
        s_state = APP_DHT_DISPLAY_STATE_RENDER;
    }
    else if ((s_screen == APP_SCREEN_HUMI_LIMIT) && ((key == 2U) || (key == 3U)))
    {
        s_humiLimit = App_DhtDisplay_AdjustLimit(s_humiLimit, HUMI_LIMIT_MAX, key);
        s_state = APP_DHT_DISPLAY_STATE_RENDER;
    }

    if (key != 0U)
    {
        App_DhtDisplay_UpdateAlarm();
    }
}

void App_DhtDisplay_Tick1ms(void)
{
    s_tickMs++;
}

void App_DhtDisplay_Init(void)
{
    DHT11_Init();
    OLED_Init();
    Key_Init();
    Alarm_Init();
    LED_Init();
    Esp32Uart_Init();

    s_sampleValid = 0U;
    s_alarmActive = 0U;
    s_remoteAlarmOn = 0U;
    s_ledOn = 0U;
    s_tempLimit = TEMP_LIMIT_DEFAULT;
    s_humiLimit = HUMI_LIMIT_DEFAULT;
    s_nextSampleMs = 0U;
    s_state = APP_DHT_DISPLAY_STATE_WAIT;
    s_screen = APP_SCREEN_MAIN;

    App_DhtDisplay_DrawScreenStatic();
    App_DhtDisplay_Render();
}

void App_DhtDisplay_Task(void)
{
    uint32_t now = s_tickMs;

    App_DhtDisplay_ProcessKey();
    App_DhtDisplay_ProcessEsp32();

    switch (s_state)
    {
        case APP_DHT_DISPLAY_STATE_WAIT:
            if (App_DhtDisplay_IsTimeReached(now, s_nextSampleMs) != 0U)
            {
                s_state = APP_DHT_DISPLAY_STATE_SAMPLE;
            }
            break;

        case APP_DHT_DISPLAY_STATE_SAMPLE:
            s_sampleValid = (DHT11_Read_TempAndHumidity(&s_dht11Data) == SUCCESS) ? 1U : 0U;
            App_DhtDisplay_UpdateAlarm();
            App_DhtDisplay_SendStatus();
            s_nextSampleMs = now + DHT11_SAMPLE_PERIOD_MS;
            s_state = APP_DHT_DISPLAY_STATE_RENDER;
            break;

        case APP_DHT_DISPLAY_STATE_RENDER:
            App_DhtDisplay_Render();
            s_state = APP_DHT_DISPLAY_STATE_WAIT;
            break;

        default:
            s_state = APP_DHT_DISPLAY_STATE_WAIT;
            break;
    }
}
