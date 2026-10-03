/************************************************************
 * 文件：RS485.h
 * 说明：GD32 RS485 统一驱动头文件 —— 编译时模式选择
 *
 * 硬件背景：
 *   RS485 是半双工差分总线。开发板 USART1 (PD5/PD6) 经收发器芯片
 *   连到 A/B 差分对，用一根 GPIO (PE8) 控制收发器的方向：
 *     DE = 高 → 驱动模式（向总线发送）
 *     DE = 低 → 接收模式（监听总线）
 *   由于半双工，同一时刻只能有一个节点在发，软件必须严格管理 DE。
 *   方向切换只能在"上一字节真正离开移位寄存器之后"做，否则截断。
 *
 * 使用方法：修改下方 RS485_RX_MODE 宏即可切换接收方式
 *   RS485_MODE_IDLE → USART1 中断接收 + IDLE 分帧
 *   RS485_MODE_DMA  → USART1 + DMA0 收发 + IDLE 分帧
 *
 * 与 USART0 的资源关系：
 *   USART0 走 DMA1 控制器（APB2），RS485(USART1) 走 DMA0 控制器（APB1），
 *   两套外设落在不同 DMA 控制器上，DMA 通道号即使同为 CH5 也互不影响。
 ************************************************************/

#ifndef __RS485_H__
#define __RS485_H__

#include "HeaderFiles.h"

/* ===================== 模式选择（二选一） ===================== */
#define RS485_MODE_IDLE 0U
#define RS485_MODE_DMA  1U

/*
 * !! 用户在此处选择接收模式，仅可取 RS485_MODE_IDLE 或 RS485_MODE_DMA !!
 *   命令帧型业务（几十字节短帧）选 IDLE 已足够省心。
 *   高吞吐 / 长帧 / CPU 紧张 → 用 DMA。
 */
#ifndef RS485_RX_MODE
#define RS485_RX_MODE RS485_MODE_DMA
#endif

/* ===================== 硬件映射（换板时只改这里） ===================== */
#define RS485_USART     USART1       /* 工业串口外设 */
#define RS485_GPIO_PORT GPIOD        /* PD5 = TX, PD6 = RX */
#define RS485_TX_PIN    GPIO_PIN_5
#define RS485_RX_PIN    GPIO_PIN_6
#define RS485_DE_PORT   GPIOE        /* PE8 = 板载 485_CS = DE/RE */
#define RS485_DE_PIN    GPIO_PIN_8

/* ===================== 缓冲区容量 ===================== */
/*
 * 256 字节足够覆盖 Modbus RTU、自定义短命令帧等常见 RS485 报文。
 * 如要传更长的批量数据帧，调大这两个值；其它代码无需改动。
 */
#define RS485_RX_BUFFER_SIZE    256U
#define RS485_FRAME_BUFFER_SIZE 256U

/* ===================== 统一公共接口 ===================== */

/**
 * @brief  初始化 RS485，按 RS485_RX_MODE 选定的模式配置接收通道。
 * @param  baudrate 串口波特率，例如 9600（工业默认）或 115200。
 *
 * 内部会完成：GPIO 复用 + DE 引脚配置 + USART1 8N1 + NVIC + 中断/DMA。
 * 调用结束后 DE 已置低，处于接收监听状态。
 */
void RS485_Init(uint32_t baudrate);

/**
 * @brief  通过 RS485 总线发送一段原始字节流（阻塞式）。
 * @param  data 待发送数据首地址，不可为 NULL。
 * @param  len  待发送字节数，不可为 0。
 *
 * 发送时序（两种模式都遵守）：
 *   ① DE 拉高 → 切到驱动模式
 *   ② 把数据交给 USART（IDLE 模式逐字节轮询，DMA 模式启动 DMA0）
 *   ③ 等 USART_FLAG_TC：移位寄存器把最后一个字节真正发完
 *      —— 这一步对 RS485 至关重要，DE 提前掉电会截断最后一位
 *   ④ DE 拉低 → 释放总线，让其他节点可以发送
 */
void RS485_SendBytes(const uint8_t *data, uint16_t len);

/**
 * @brief  格式化打印到 RS485（推荐使用）。
 * @param  format 格式化字符串（同 printf）。
 * @param  ...    可变参数。
 * @return 实际发送的字节数。
 *
 * 内部用 vsnprintf 一次性格式化到栈缓冲区，再调用一次 RS485_SendBytes，
 * 整段数据在一次 DE 高电平期间发出，不会出现"每个字符切一次方向"的低效。
 */
int RS485_Printf(const char *format, ...);

/**
 * @brief  从帧缓冲区中取出一帧由 IDLE 分帧得到的完整数据。
 * @param  out_buf 用户输出缓冲区。
 * @param  out_len 输出帧实际长度指针。
 * @param  max_len out_buf 最大容量。
 * @return 1 = 成功取到一帧；0 = 当前无完整帧。
 *
 * 主循环高频轮询调用即可。取走帧后内部自动清"帧就绪"标志，等待下一帧。
 */
uint8_t RS485_FetchFrame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len);

/* =============== 模式专属接口（条件编译） =============== */

#if (RS485_RX_MODE == RS485_MODE_IDLE)
/** 查询接收缓冲区是否曾发生溢出（一帧超过 RS485_RX_BUFFER_SIZE 时置位）。 */
uint8_t RS485_GetOverflowFlag(void);
/** 清除溢出标志，避免下一轮重复进入溢出处理分支。 */
void    RS485_ClearOverflowFlag(void);
#endif

#if (RS485_RX_MODE == RS485_MODE_DMA)
/** 查询 DMA 发送是否仍在搬运（当前 SendBytes 是阻塞实现，主要给将来非阻塞预留）。 */
uint8_t RS485_IsBusy(void);
#endif

#endif /* __RS485_H__ */
