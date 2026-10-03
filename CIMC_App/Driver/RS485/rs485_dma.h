/************************************************************
 * 文件：rs485_dma.h
 * 说明：GD32 RS485 DMA 收发 + IDLE 分帧 —— 内部实现头文件
 *        仅供 RS485.c 引入，不对外暴露。
 *
 * DMA 资源说明：
 *   USART1 在 GD32F4xx 上由 DMA0 控制器（APB1 域）服务，
 *   USART0 走 DMA1 控制器（APB2 域）；两套外设落在不同 DMA 控制器，
 *   通道号即使同为 CH5 也互不干扰。ADC0 也走 DMA1，与 RS485 无冲突。
 ************************************************************/

#ifndef __RS485_DMA_H__
#define __RS485_DMA_H__

#include "RS485.h"

/* DMA 通道号 —— 多次使用，保留为宏 */
#define RS485_DMA_TX_CHANNEL DMA_CH6 /* USART1_TX → DMA0 CH6 SUBPERI4 */
#define RS485_DMA_RX_CHANNEL DMA_CH5 /* USART1_RX → DMA0 CH5 SUBPERI4 */

void    rs485_dma_init(uint32_t baudrate);
void    rs485_dma_send_bytes(const uint8_t *data, uint16_t len);
uint8_t rs485_dma_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);
uint8_t rs485_dma_is_busy(void);

#endif /* __RS485_DMA_H__ */
