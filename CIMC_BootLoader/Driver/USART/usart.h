/************************************************************
 * 版权：2025CIMC Copyright。 
 * 文件：usart.h
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2025/12/30     V0.01    original
************************************************************/
#ifndef __USART_H__
#define __USART_H__
/************************* 头文件 *************************/

#include <stdbool.h>
#include "HeaderFiles.h"


/************************* 宏定义 *************************/
#define USART_PORT GPIOD
#define USART USART1
#define USART_TX_Pin GPIO_PIN_5
#define USART_RX_Pin GPIO_PIN_6
#define USART_RCU RCU_USART1
#define USART_PIN_RCU RCU_GPIOD
#define USART_IRQn USART1_IRQn

#define RS485_DE_PORT GPIOE
#define RS485_DE_PIN GPIO_PIN_8
#define RS485_DE_RCU RCU_GPIOE

/************************ 变量定义 ************************/


/************************ 函数定义 ************************/

void my_usart_init(uint32_t baudrate);

void usart_recv_buf(void);

bool bl_usart_read_byte(uint8_t *byte);

void bl_usart_clear_rx_stream(void);

#endif // !__USART_H__

/****************************End*****************************/
