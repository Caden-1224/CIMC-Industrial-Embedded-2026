#ifndef __LED_H
#define __LED_H

#include "HeaderFiles.h"

/************************ LED 引脚硬件映射 ************************/

/** LED 所在 GPIO 端口 */
#define LED_PORT GPIOE

/** LED1 ~ LED6 对应引脚，按板级原理图配置 */
#define LED1_PIN GPIO_PIN_0
#define LED2_PIN GPIO_PIN_1
#define LED3_PIN GPIO_PIN_3
#define LED4_PIN GPIO_PIN_4
#define LED5_PIN GPIO_PIN_5
#define LED6_PIN GPIO_PIN_6

/************************ 接口 ************************/

void LED_Init(void);

#endif
