#ifndef __USART_SERVICE_H
#define __USART_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  USART0 服务初始化 —— 配置 115200 波特率并打印启动提示
 */
void Usart_Service_Init(void);

/**
 * @brief  USART0 周期任务 —— 轮询帧接收并处理指令
 */
void Usart_Service_Task(void);

/**
 * @brief  从 USART0 接收两位 ASCII 数字字符，校验并返回十进制数值
 * @param  value  输入值的十进制上限（如年份=99，月份=12，小时=23）
 * @retval 成功返回十进制值 (0~value)，失败返回 0xFF（输入非数字字符或超出上限）
 *
 * @note   工作流程：
 *         [步骤1] 字符接收与 ASCII 校验
 *         循环接收 2 个字节，逐个检查是否在 '0'(0x30) ~ '9'(0x39) 范围内
 *         非数字字符 → 打印提示并回退索引，要求重新输入该位
 *
 *         [步骤2] ASCII → 十进制数值转换
 *         index = (个位 - 0x30) + (十位 - 0x30) * 10
 *
 *         [步骤3] 范围校验
 *         若十进制数值 > value（入参上限）→ 打印提示，返回 0xFF
 *
 *         不做 BCD 转换，调用方按需自行编码
 */
uint8_t Usart_InputDecimal(uint32_t value);

#endif
