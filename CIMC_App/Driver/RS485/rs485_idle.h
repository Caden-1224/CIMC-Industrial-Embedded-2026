/************************************************************
 * 文件：rs485_idle.h
 * 说明：GD32 RS485 中断接收 + IDLE 分帧 —— 内部实现头文件
 *        仅供 RS485.c 引入，不对外暴露。
 ************************************************************/

#ifndef __RS485_IDLE_H__
#define __RS485_IDLE_H__

#include "RS485.h"

void    rs485_idle_init(uint32_t baudrate);
void    rs485_idle_send_bytes(const uint8_t *data, uint16_t len);
uint8_t rs485_idle_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);
uint8_t rs485_idle_get_overflow_flag(void);
void    rs485_idle_clear_overflow_flag(void);

#endif /* __RS485_IDLE_H__ */
