#ifndef BSP_KEY_H
#define BSP_KEY_H

#include "stm32f10x.h"

#define KEY1_PORT_CLK  RCC_APB2Periph_GPIOA
#define KEY1_PORT	  	 GPIOA
#define KEY1_PORT_PIN  GPIO_Pin_5

#define KEY2_PORT_CLK  RCC_APB2Periph_GPIOA
#define KEY2_PORT	  	 GPIOA
#define KEY2_PORT_PIN  GPIO_Pin_6

#define KEY3_PORT_CLK  RCC_APB2Periph_GPIOA
#define KEY3_PORT	     GPIOA
#define KEY3_PORT_PIN  GPIO_Pin_7

#define KEY1    GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_5) //读取按键1
#define KEY2    GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_6) //读取按键1
#define KEY3    GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_7) //读取按键1

void Key_Init(void);
uint8_t Key_Scan(uint8_t mode);
#endif