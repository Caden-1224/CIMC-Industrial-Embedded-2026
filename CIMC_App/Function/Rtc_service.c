/************************************************************
 * 版权�?025CIMC Copyright�? * 文件：Rtc_service.c
 * 作�? Caden
 * 平台: 2025CIMC IHD-V04
 * 版本: Caden        2026/5/10    V0.04    分层重构：纯 RTC 业务逻辑
 *
 * 说明：RTC 服务层实现�? *       本层不直接操�?RTC 寄存器，而是�? *       - 调用硬件�?RTC_HwInit() 完成时钟初始�? *       - 调用硬件�?RTC_SetTime() / RTC_GetTime() 等接口操�?RTC
 *       - 通过 RTC_IsConfigured() 判断配置状�? *       - 通过 RTC_Service_Task() 周期检�?RTC 健康状�? *       本层不涉�?LED 控制、串口输入等 UI 逻辑�? ************************************************************/

#include "Rtc_service.h"
#include "RTC.h"
#include "USART.h"

/* ============================ 内部变量 ============================ */

/** RTC 健康状态：0 = 未检�?异常�? = 正常（秒值在递增�?*/
uint8_t rtc_healthy = 0;

/** 上次采样的秒值（BCD 编码），用于判断 RTC 是否在走�?*/
static uint8_t s_prev_second = 0xFF;

/** 连续采样中秒值未变化的计数，超过阈值判定为 RTC 停走 */
static uint8_t s_stale_count = 0;

/**
 * @brief  连续多少�?Task 调用秒值不变即判定异常
 *         �?Task 周期�?100ms，则 10 �?= 1 秒内秒值未�?�?异常
 *         正常情况�?1 秒内至少跳变 1 �? */
#define RTC_STALE_THRESHOLD  10u

/* ============================ 服务层公共接口实�?============================ */

/*!
    \brief      RTC 服务初始�?—�?完整初始化流程编�?    \param[in]  none
    \param[out] none
    \retval     none

    \details
    完整初始化流程（按执行顺序）�?
    [阶段1] 硬件层初始化
    - 调用 RTC_HwInit() 完成备份域使能、时钟源选择、预分频器计�?    - 硬件层内部封装了 PMU 时钟使能、备份域写保护解除、振荡器使能等全部底层操�?
    [阶段2] 配置状态判�?    - 调用 RTC_IsConfigured() 检查备份寄存器魔术�?+ 时钟源是否已配置
    - 若未配置 �?上层负责处理首次配置（非 RTC Service 职责�?    - 若已配置 �?检测复位源并显示当前时�?
    [阶段3] 收尾
    - 清除所有复位标志（�?RTC_PrintResetSource() 内部完成�?*/
void RTC_Service_Init(void)
{
    USART0_Printf("\n\r  ****************** RTC calendar demo ******************\n\r");

    /* === 阶段1：硬件层初始�?=== */
    /* RTC_HwInit() 内部封装了：
     * - PMU 时钟使能 + 备份域写保护解除
     * - 振荡器使�?+ 等待稳定
     * - RTC 时钟源切�?+ 预分频器计算
     * - RTC 外设时钟使能 + 寄存器同步等�?*/
    RTC_HwInit();

    /* === 阶段2：判�?RTC 是否已配�?=== */
    /* RTC_IsConfigured() 内部检查：
     *   �?备份寄存�?BKP0 魔术字是否匹配（VBAT 是否掉电过）
     *   �?时钟源位段是否非零（是否从未配置过时钟源�?     * 任一条件不满足返�?0 */
    if (!RTC_IsConfigured())
    {
        /* 未配置：上层负责处理首次时间配置
         * 配置完成后应调用 RTC_SetTime() + RTC_SetConfigured() */
        USART0_Printf("RTC not configured, waiting for external setup....\n\r");
    }
    else
    {
        RTC_PrintResetSource();
        /* === 阶段3：RTC 已配�?—�?检测复位源并显示时�?=== */
        /* RTC_PrintResetSource() 内部完成�?         *   �?读取 RCU 复位标志并打印复位类�?         *   �?清除所有复位标志，避免影响后续判断 */

        USART0_Printf("no need to configure RTC....\n\r");

        /* 读取并显�?RTC 当前保持的时�?*/
        RTC_Service_ShowTime();
    }

    /* 初始化健康检测状�?*/
    s_prev_second = 0xFF;
    s_stale_count = 0;
    rtc_healthy   = 0;
}

/*!
    \brief      RTC 周期任务 —�?由时间片调度器调�?    \param[in]  none
    \param[out] none
    \retval     none

    \details
    最小健康检测：通过判断秒值是否在递增来检�?RTC 是否正常走时�?
    检测原理：
    - 每次调用读取 RTC 当前秒值（BCD 编码�?    - 与上次采样值比较：
      · 秒值变�?�?RTC 正在走时 �?清零计数器，标记健康
      · 秒值不�?�?累加 stale_count，超过阈值判定为 RTC 停走
    - 首次调用（s_prev_second == 0xFF）仅记录初始值，不判�?
    阈值说明：
    - RTC_STALE_THRESHOLD = 10，即连续 10 �?Task 调用秒值不�?�?异常
    - �?Task 周期 100ms，则 1 秒内秒值未跳变即判定异�?    - 正常情况�?1 秒至少跳�?1 次，留有余量

    输出�?    - 更新 rtc_healthy 全局变量�?=异常�?=正常�?    - 外部模块（如 Led_service）读取该变量控制 LED 状�?*/
void RTC_Service_Task(void)
{
    rtc_time_t now;
    RTC_GetTime(&now);

    /* 首次调用：仅记录初始秒值，不判�?*/
    if (s_prev_second == 0xFF)
    {
        s_prev_second = now.second;
        return;
    }

    /* 秒值变�?�?RTC 正在走时 */
    if (now.second != s_prev_second)
    {
        s_prev_second = now.second;
        s_stale_count = 0;
        rtc_healthy   = 1;
    }
    else
    {
        RTC_PrintResetSource();
        /* 秒值未�?�?累加计数 */
        s_stale_count++;

        if (s_stale_count >= RTC_STALE_THRESHOLD)
        {
            /* 连续超过阈值次秒值未�?�?判定 RTC 停走 */
            rtc_healthy = 0;
        }
    }
}

/*!
    \brief      在串口控制台显示当前 RTC 时间
    \param[in]  none
    \param[out] none
    \retval     none

    \details
    调用硬件�?RTC_GetTime() 读取当前时间�?    然后通过 printf 格式化输出到串口控制台�?
    格式化说明：
    - %0.2x：以十六进制输出，不�?2 位左侧补 0
    - RTC 内部�?BCD 格式存储，所以十六进制输出看起来像十进制
    - 例如：月�?4 �?�?BCD 0x04 �?printf %0.2x �?"04"
    - 年份前缀 "20" 硬编码，适用范围 2000~2099 �?*/
void RTC_Service_ShowTime(void)
{
    rtc_time_t time;

    /* 通过硬件层接口读取当前时间（BCD 格式�?*/
    RTC_GetTime(&time);

    /* 输出格式：Current time: 20YY-MM-DD : HH:MM:SS
     * BCD 值以十六进制输出天然就是十进制格�?*/
    USART0_Printf("\r\nCurrent time: 20%0.2x-%0.2x-%0.2x", time.year, time.month, time.day);
    USART0_Printf(" : %0.2x:%0.2x:%0.2x \r\n", time.hour, time.minute, time.second);
}

/*!
    \brief      在串口控制台显示当前 RTC ALARM0 的闹钟设定�?    \param[in]  none
    \param[out] none
    \retval     none

    \details
    调用硬件�?RTC_GetAlarm() 读取闹钟设定值，
    然后格式化输出时:�?秒�?*/
void RTC_Service_ShowAlarm(void)
{
    rtc_time_t alarm;

    /* 通过硬件层接口读取闹钟设定值（BCD 格式�?*/
    RTC_GetAlarm(&alarm);

    USART0_Printf("The alarm: %0.2x:%0.2x:%0.2x \n\r", alarm.hour, alarm.minute, alarm.second);
}
