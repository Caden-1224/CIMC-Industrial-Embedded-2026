#ifndef __KEY_H
#define __KEY_H

#include "HeaderFiles.h"

/************************ KEY 引脚硬件映射 ************************/

/** KEY 所在 GPIO 端口 */
#define KEY_PORT GPIOD
/** KEY1 ~ KEY6 对应引脚，按板级原理图配置 */
#define KEY1_PIN GPIO_PIN_10
#define KEY2_PIN GPIO_PIN_11
#define KEY3_PIN GPIO_PIN_12
#define KEY4_PIN GPIO_PIN_13
#define KEY5_PIN GPIO_PIN_14
#define KEY6_PIN GPIO_PIN_15
/************************ 接口 ************************/

/**
 * @brief  初始化所有 KEY 引脚为浮空输入（外部上拉，按下接地）。
 */
void KEY_Init(void);

/**
 * @brief  按键按下沿检测（非阻塞）。
 * @param  port  KEY 所在 GPIO 端口（必须是 KEY_PORT）。
 * @param  pin   KEYx_PIN 之一。
 * @return 1 = 本次轮询检测到"高→低"按下沿；0 = 没按 / 仍在按住 / 引脚非法。
 *
 * 与时间片调度配合使用：
 *   函数无任何 delay 与 while 等待，仅做一次电平读取 + 上次状态比较。
 *   按住不放只在按下瞬间返回一次 1，松开过程不触发事件。
 *   依赖固定周期调用（典型由 Key_Service_Task 以 20ms 节拍调度）来完成天然消抖。
 */
uint8_t KEY_Stat(uint32_t port, uint16_t pin);

#endif



