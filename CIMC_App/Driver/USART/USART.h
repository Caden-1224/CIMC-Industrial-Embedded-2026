/************************************************************
 * 文件：USART.h
 * 说明：GD32 USART0 统一驱动头文件 —— 编译时模式选择
 *
 * 使用方法：修改下方 USART0_RX_MODE 宏即可切换接收模式
 *   USART0_MODE_IDLE  → 中断接收 + IDLE 分帧
 *   USART0_MODE_DMA   → DMA 收发 + IDLE 分帧
 ************************************************************/

#ifndef __USART_H__
#define __USART_H__

#include "HeaderFiles.h"

/* ===================== 模式选择（二选一） ===================== */
#define USART0_MODE_IDLE 0U
#define USART0_MODE_DMA 1U

/*
 * !! 用户在此处选择接收模式，仅可取 USART0_MODE_IDLE 或 USART0_MODE_DMA !!
 */
#ifndef USART0_RX_MODE
#define USART0_RX_MODE USART0_MODE_DMA
#endif

/* ===================== 串口硬件映射 ===================== */
#define USART0_PORT GPIOA
#define USART0_PERIPH USART0
#define USART0_TX_PIN GPIO_PIN_9
#define USART0_RX_PIN GPIO_PIN_10
#define USART0_GPIO_RCU RCU_GPIOA
#define USART0_USART_RCU RCU_USART0
#define USART0_IRQn USART0_IRQn
#define USART0_GPIO_AF GPIO_AF_7

/* ===================== 缓冲区大小 ===================== */
#if (USART0_RX_MODE == USART0_MODE_IDLE)
#define USART0_RX_BUFFER_SIZE 256U
#define USART0_FRAME_BUFFER_SIZE 256U
#elif (USART0_RX_MODE == USART0_MODE_DMA)
#define USART0_TX_BUFFER_SIZE 256U
#define USART0_RX_BUFFER_SIZE 32768U
#define USART0_FRAME_BUFFER_SIZE 32768U
#endif

/* ===================== 格式化输出缓冲区 ===================== */

/**
 * USART0_Printf() 内部用于暂存格式化结果的缓冲区大小。
 * 256 字节足够绝大多数调试输出场景，如需更长可自行改大。
 */
#define USART0_PRINTF_BUF_SIZE 256U

/* ===================== 统一公共接口 ===================== */

/**
 * @brief  初始化 USART0，按 USART0_RX_MODE 选定的模式配置接收通道。
 * @param  baudrate: 串口波特率，例如 115200。
 */
void USART0_Init(uint32_t baudrate);

/**
 * @brief  发送一段数据（阻塞式）。
 * @param  data: 待发送数据首地址。
 * @param  len:  待发送数据长度，单位字节。
 */
void USART0_SendBytes(const uint8_t *data, uint16_t len);

/**
 * @brief  格式化打印到串口（推荐使用）。
 *
 * 用法与 printf 完全一致：
 *   USART0_Printf("temp = %d, name = %s\r\n", temp, name);
 *
 * 比 fputc 重定向方式高效得多：
 *   fputc 方式：printf 内部逐字符调用 fputc → 每个字符触发一次
 *               USART0_SendBytes（DMA 模式下就是一次完整的 DMA 配置），
 *               打印 "hello 42\r\n" 要触发 10+ 次 DMA 启停。
 *   本函数：先用 vsnprintf 把整个格式化结果写入内部缓冲区，
 *           再一次性调用 USART0_SendBytes 发送，只触发 1 次。
 *
 * @param  format: 格式化字符串（同 printf）。
 * @param  ...:    可变参数列表。
 * @return 实际发送的字节数。
 */
int USART0_Printf(const char *format, ...);

/**
 * @brief  取出一帧由 IDLE 分帧得到的完整数据。
 * @param  out_buf: 用户输出缓冲区。
 * @param  out_len: 输出帧实际长度指针。
 * @param  max_len: out_buf 最大容量。
 * @return 1 = 成功取到一帧，0 = 当前无完整帧。
 */
uint8_t USART0_FetchFrame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);

/* =============== 模式专属接口（条件编译） =============== */

#if (USART0_RX_MODE == USART0_MODE_IDLE)
uint8_t USART0_GetOverflowFlag(void);
void USART0_ClearOverflowFlag(void);
#endif

#if (USART0_RX_MODE == USART0_MODE_DMA)
uint8_t USART0_IsBusy(void);
#endif

#endif /* __USART_H__ */
