/************************************************************
 * 文件：usart_idle.h
 * 说明：GD32 USART0 中断接收 + IDLE 分帧 —— 内部实现头文件
 *        仅供 USART.c 引入，不对外暴露。
 ************************************************************/

#ifndef __USART_IDLE_H__
#define __USART_IDLE_H__

#include "USART.h"

void     usart_idle_init(uint32_t baudrate);
void     usart_idle_send_bytes(const uint8_t *data, uint16_t len);
uint8_t  usart_idle_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);
uint8_t  usart_idle_get_overflow_flag(void);
void     usart_idle_clear_overflow_flag(void);

#endif /* __USART_IDLE_H__ */
