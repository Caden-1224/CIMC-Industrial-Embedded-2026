/************************************************************
 * 版权所有：2025 CIMC Copyright
 * 文件名称：OLED_IIC.h
 * 作    者：Lingyu Meng
 * 维护作者：Caden
 * 版    本：V0.03
 * 说    明：板载 OLED 显示屏的软件模拟 I2C (IIC) 硬件驱动。
 *
 * === I2C 协议工作流程概述 ===
 *
 * I2C 是一种只需要 2 根线的串行总线：
 *   - SCL (时钟线)：由主机控制，每次电平跳变指示一个时间节拍。
 *   - SDA (数据线)：主机和从机都可以驱动，用来传输实际数据。
 *
 * 一次完整的 I2C 写入事务流程如下：
 *
 *   1. 主机发出 START 信号 —— 在 SCL 高电平期间，把 SDA 从高拉到低。
 *      这个动作告诉所有从机："注意，有新事务要开始了"。
 *
 *   2. 主机发送 7 位从机地址 + 1 位读写方向 (R/W)。
 *      - 本 OLED 的 7 位地址是 0x3C，左移 1 位后写模式为 0x78。
 *      - 每一位都在 SCL 的上升沿被从机采样。
 *
 *   3. 从机在第 9 个时钟脉冲期间拉低 SDA 作为 ACK (应答)。
 *      - 主机释放 SDA 后检测电平：低电平 = 从机回应了；高电平 = 没人理。
 *
 *   4. 主机继续发送控制字节或数据字节，每 8 位后都检查一次 ACK。
 *      - 对 SSD1306 来说，控制字节决定后续内容是命令还是显示数据。
 *
 *   5. 主机发出 STOP 信号 —— 在 SCL 高电平期间，把 SDA 从低拉到高。
 *      这个动作告诉从机："本次事务结束，释放总线"。
 *
 * 本工程采用"软件模拟 I2C"（GPIO 位操作），而不是 GD32 的硬件 I2C 外设。
 * 原因是 OLED 对时序要求不高，软件模拟更灵活，也更容易跨平台移植。
 *
 * === 引脚分配 ===
 *   - PB8 → SCL (时钟线)
 *   - PB9 → SDA (数据线)
 *   两根线均配置为开漏输出 + 上拉电阻，符合 I2C 规范。
 ************************************************************/

#ifndef __OLED_IIC_H
#define __OLED_IIC_H

#include "HeaderFiles.h"

/* 兼容旧代码的类型别名 */
#define u8  unsigned char
#define u32 unsigned int

#define OLED_ADDR   0x78U
#define OLED_WIDTH  128U
#define OLED_HEIGHT 32U

/* --- 引脚定义 --- */
/* SCL 时钟线挂在 GPIOB 第 8 脚，SDA 数据线挂在 GPIOB 第 9 脚 */
#define OLED_SCL_PORT    GPIOB
#define OLED_SCL_PIN     GPIO_PIN_8
#define OLED_SDA_PORT    GPIOB
#define OLED_SDA_PIN     GPIO_PIN_9

/* --- SCL/SDA 电平控制宏 ---
 * I2C 总线是开漏结构，"Set" 等于释放总线（由上拉电阻拉到高），
 * "Clr" 等于主动拉低。所有时序由这几个宏组合产生。 */
#define OLED_SCLK_Set()     gpio_bit_set(OLED_SCL_PORT, OLED_SCL_PIN)
#define OLED_SCLK_Clr()     gpio_bit_reset(OLED_SCL_PORT, OLED_SCL_PIN)
#define OLED_SDIN_Set()     gpio_bit_set(OLED_SDA_PORT, OLED_SDA_PIN)
#define OLED_SDIN_Clr()     gpio_bit_reset(OLED_SDA_PORT, OLED_SDA_PIN)

/* 读取 SDA 当前电平，用于检测从机 ACK 信号 */
#define IIC_READ_SDA        gpio_input_bit_get(OLED_SDA_PORT, OLED_SDA_PIN)

/* --- OLED 命令/数据标志 ---
 * SSD1306 的 I2C 控制字节中，bit6=0 表示接下来是命令，bit6=1 表示是显示数据。
 * 外部调用时用这两个宏来区分。 */
#define OLED_CMD   0   /* 控制命令：配置显示参数、设置地址等 */
#define OLED_DATA  1   /* 显示数据：真正要显示的像素内容 */

/* === I2C 总线底层原语 ===
 * 以下 4 个函数实现了 I2C 协议的基本信号时序，
 * 所有上层的 OLED 操作最终都通过这 4 个函数完成总线交互。 */

/**
 * @brief 产生 I2C 起始信号 (START)。
 * @details 时序：先确保 SCL 和 SDA 都为高，然后在 SCL 保持高电平期间
 *          将 SDA 拉低。这个"高到低"的跳变就是 START 信号。
 */
void I2C_Start(void);

/**
 * @brief 产生 I2C 停止信号 (STOP)。
 * @details 时序：先确保 SDA 为低、SCL 为高，然后在 SCL 保持高电平期间
 *          将 SDA 从低拉到高。这个"低到高"的跳变就是 STOP 信号。
 */
void I2C_Stop(void);

/**
 * @brief 等待从机应答 (ACK/NACK)。
 * @return uint8_t 收到 ACK 返回 1，超时无应答返回 0。
 * @details 主机发送完 8 位数据后，释放 SDA 并产生第 9 个时钟脉冲。
 *          如果从机把 SDA 拉低了，说明它应答了；如果 SDA 一直为高，
 *          说明从机没有回应（地址错误或设备离线）。
 */
uint8_t I2C_WaitAck(void);

/**
 * @brief 通过 I2C 总线逐位发送 1 个字节。
 * @param dat 需要发送的 8 位数据，高位 (MSB) 先发。
 * @details 循环 8 次：每次在 SCL 低电平期间设置 SDA 电平，
 *          然后 SCL 拉高让从机在上升沿采样。发完后由调用方负责检查 ACK。
 */
void Send_Byte(u8 dat);

/* === OLED 核心接口 === */

/**
 * @brief 向 SSD1306 写入 1 个字节（命令或数据）。
 * @param dat 要写入的字节内容。
 * @param mode OLED_CMD (0) 表示命令，OLED_DATA (1) 表示显示数据。
 * @details 完整的 I2C 写入事务流程：
 *          START → 发送从机地址 (0x78) → 等待 ACK →
 *          发送控制字节 (0x00=命令 / 0x40=数据) → 等待 ACK →
 *          发送实际字节 → 等待 ACK → STOP。
 */
void OLED_WR_Byte(u8 dat, u8 mode);
void OLED_Write_cmd(uint8_t cmd);
void OLED_Write_data(uint8_t data);
void OLED_Set_Position(uint8_t x, uint8_t y);
void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize);
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[]);
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no);
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n);
void OLED_Allfill(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);

/* === 显示控制 === */

void OLED_DisPlay_On(void);
void OLED_DisPlay_Off(void);
void OLED_Refresh(void);
void OLED_Clear(void);

/* === 绘图原语 === */

void OLED_DrawPoint(u8 x, u8 y);
void OLED_ClearPoint(u8 x, u8 y);
void OLED_DrawLine(u8 x1, u8 y1, u8 x2, u8 y2);
void OLED_DrawCircle(u8 x, u8 y, u8 r);

/* === 文本显示 === */

void OLED_ShowChar(u8 x, u8 y, u8 chr, u8 size1);
void OLED_ShowString(u8 x, u8 y, u8 *chr, u8 size1);
void OLED_ShowNum(u8 x, u8 y, u32 num, u8 len, u8 size1);
void OLED_ShowChinese(u8 x, u8 y, u8 num, u8 size1);
void OLED_ScrollDisplay(u8 num, u8 space);
void OLED_ShowPicture(u8 x0, u8 y0, u8 x1, u8 y1, u8 BMP[]);

/* === 工具函数 === */

void OLED_ColorTurn(u8 i);
void OLED_DisplayTurn(u8 i);
void OLED_WR_BP(u8 x, u8 y);

/**
 * @brief 初始化 OLED 硬件：配置 GPIO 引脚 + 发送 SSD1306 初始化命令序列。
 * @details 初始化流程：
 *          1. 使能 GPIOB 时钟
 *          2. 将 PB8(SCL) 和 PB9(SDA) 配置为开漏输出 + 上拉
 *          3. 等待 500ms 确保 OLED 上电稳定
 *          4. 逐条发送 SSD1306 初始化命令（关闭显示→设置参数→开启显示）
 *          5. 清屏并刷新第一帧
 */
void OLED_Init(void);

#endif

