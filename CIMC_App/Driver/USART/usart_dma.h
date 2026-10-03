/************************************************************
 * 文件：usart_dma.h
 * 说明：GD32 USART0 DMA 收发 + IDLE 分帧 —— 内部实现头文件
 *        仅供 USART.c 引入，不对外暴露。
 ************************************************************/

#ifndef __USART_DMA_H__
#define __USART_DMA_H__

#include "USART.h"

/* DMA 硬件映射 */
#define USART_DMA_RCU                    RCU_DMA1
#define USART_DMA_TX_DMA                 DMA1
#define USART_DMA_TX_CHANNEL             DMA_CH7
#define USART_DMA_RX_DMA                 DMA1
#define USART_DMA_RX_CHANNEL             DMA_CH5
#define USART_DMA_SUBPERI                DMA_SUBPERI4
#define USART_DMA_DATA_ADDR              ((uint32_t)&USART_DATA(USART0))

void     usart_dma_init(uint32_t baudrate);
void     usart_dma_send_bytes(const uint8_t *data, uint16_t len);
uint8_t  usart_dma_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);
uint8_t  usart_dma_is_busy(void);

#endif /* __USART_DMA_H__ */
