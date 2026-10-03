/************************************************************
 * 版权所有：2025CIMC Copyright
 * 文件名称：Led_service.h
 * 平    台：2025CIMC IHD-V04
 ************************************************************/
#ifndef __LED_SERVICE_H
#define __LED_SERVICE_H

#include "HeaderFiles.h"

#define LED_NUM 6 /**< LED 总数，按实际板载数量配置 */

/**
 * @brief  LED 状态数组，外部可写
 *         led_state[i] != 0 → 第 i 位 LED 点亮
 *         led_state[i] == 0 → 第 i 位 LED 熄灭
 *         索引 0-3 对应 LED1-LED4，4-5 预留给 LED5-LED6
 */
extern uint8_t led_state[LED_NUM];

void Led_Service_Init(void);
void Led_Service_Task(void);

#endif
