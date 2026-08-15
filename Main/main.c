#include "stm32f10x.h"
#include "bsp_delay.h"
#include "bsp_timer.h"
#include "app_dht_display.h"

int main(void)
{
    Delay_Init();
    GENERAL_TIM_Init();
    App_DhtDisplay_Init();

    while (1)
    {
        App_DhtDisplay_Task();
    }
}