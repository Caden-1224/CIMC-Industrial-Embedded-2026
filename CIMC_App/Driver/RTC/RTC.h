/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：RTC.h
 * 作者: Caden
 * 平台: 2025CIMC IHD-V04
 * 版本: Caden        2026/5/10    V0.04    分层重构：硬件抽象层接口定义
 *
 * 说明：RTC 硬件抽象层头文件。
 *       本层封装所有寄存器操作和 GD32 标准库调用，
 *       服务层通过本文件声明的函数接口访问 RTC 硬件，
 *       禁止直接操作寄存器或标准库结构体。
 *
 *       隐藏的内部实现（不在本文件暴露）：
 *       - 备份寄存器读写（BKP0 魔术字）
 *       - 时钟源配置检查（RCU_BDCTL 位段）
 *       - 复位源检测与清除（RCU_FLAG_xxx）
 ************************************************************/

#ifndef __RTC_H
#define __RTC_H

#include "HeaderFiles.h"

/* ============================ 配置宏 ============================ */

/**
 * @brief  RTC 时钟源选择（编译期宏，二选一）
 *        LXTAL  —— 外部 32.768kHz 低速晶体（精度高，需外接晶振）
 *        IRC32K —— 内部 32kHz RC 振荡器（精度低，无需外接元件）
 */
#define RTC_CLOCK_SOURCE_LXTAL
// #define RTC_CLOCK_SOURCE_IRC32K

/* ============================ 数据类型 ============================ */

/**
 * @brief  RTC 时间结构体
 *         所有字段均为 BCD 编码，与 RTC 寄存器格式一致
 *         例：2026年5月10日 14:30:45
 *             → year=0x26, month=0x05, day=0x10
 *             → hour=0x14, minute=0x30, second=0x45
 *
 *         BCD 编码原理：
 *         - 十进制 26 → BCD: 0010 0110 = 0x26
 *         - 高 4 位存十位数字（0010 = 2），低 4 位存个位数字（0110 = 6）
 */
typedef struct {
    uint8_t year;    /**< 年份后两位 BCD (0x00~0x99) */
    uint8_t month;   /**< 月份 BCD (0x01~0x12) */
    uint8_t day;     /**< 日期 BCD (0x01~0x31) */
    uint8_t hour;    /**< 小时 BCD (0x00~0x23) */
    uint8_t minute;  /**< 分钟 BCD (0x00~0x59) */
    uint8_t second;  /**< 秒钟 BCD (0x00~0x59) */
} rtc_time_t;

/* ============================ 硬件层接口 ============================ */

/**
 * @brief  RTC 硬件完整初始化
 *         封装以下底层操作（服务层无需感知）：
 *         1. 使能 PMU 时钟 + 解除备份域写保护
 *         2. 使能所选振荡器并等待稳定
 *         3. 切换 RTC 时钟源到所选振荡器
 *         4. 计算并设置预分频值（目标 ck_spre = 1Hz）
 *         5. 使能 RTC 外设时钟并等待寄存器同步
 */
void RTC_HwInit(void);

/**
 * @brief  检查 RTC 是否已配置过
 *         内部实现：读备份寄存器 BKP0 比对魔术字 + 检查时钟源位段
 *         Service 层无需了解备份寄存器和 RCU 寄存器细节
 * @retval 0 = 未配置（首次上电或 VBAT 掉电），1 = 已配置
 */
uint8_t RTC_IsConfigured(void);

/**
 * @brief  标记 RTC 为"已配置"状态
 *         内部实现：将魔术字写入备份寄存器 BKP0
 * @note   调用时机：上层完成时间配置后调用
 */
void RTC_SetConfigured(void);

/**
 * @brief  设置 RTC 时间
 *         封装标准库 rtc_init()，将 rtc_time_t 中的时间写入 RTC 寄存器
 *         预分频值由 RTC_HwInit() 已计算好，本函数自动填入
 * @param  time 指向 rtc_time_t 结构体，所有字段为 BCD 编码
 * @retval 0 = 失败，1 = 成功
 */
uint8_t RTC_SetTime(const rtc_time_t *time);

/**
 * @brief  获取 RTC 当前时间
 *         封装标准库 rtc_current_time_get()，通过影子寄存器确保读取原子性
 * @param  time [out] 指向 rtc_time_t 结构体，函数填充 BCD 编码值
 */
void RTC_GetTime(rtc_time_t *time);

/**
 * @brief  获取 RTC ALARM0 闹钟设定值
 *         封装标准库 rtc_alarm_get(RTC_ALARM0, ...)
 * @param  time [out] 指向 rtc_time_t 结构体，函数填充时分秒字段
 */
void RTC_GetAlarm(rtc_time_t *time);

/**
 * @brief  检测并打印复位源信息
 *         内部实现：读 RCU 复位标志（PORRST / EPRST），
 *         通过 printf 输出复位类型，然后清除所有复位标志
 * @note   Service 层只需调用，无需了解 RCU_FLAG_xxx 枚举
 */
void RTC_PrintResetSource(void);

#endif
