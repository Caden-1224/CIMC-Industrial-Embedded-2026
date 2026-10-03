/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：RTC.c
 * 作者: Caden
 * 平台: 2025CIMC IHD-V04
 * 版本: Caden        2026/5/10    V0.04    分层重构：硬件抽象层实现
 *
 * 说明：RTC 硬件抽象层实现。
 *       本文件封装所有寄存器操作和 GD32 标准库调用，
 *       服务层通过 RTC.h 声明的函数接口访问 RTC 硬件，
 *       禁止直接操作寄存器或标准库结构体。
 *
 *       本文件内部隐藏了以下实现细节：
 *       - 备份寄存器 BKP0 读写（魔术字机制）
 *       - RCU_BDCTL 时钟源位段检查
 *       - RCU 复位标志检测与清除
 ************************************************************/

#include "RTC.h"

/* ============================ 内部宏 ============================ */

/**
 * @brief 备份寄存器魔术字
 *        上电后读取 BKP0，若值 != BKP_VALUE，说明首次上电或 VBAT 掉电过，
 *        此时需要重新配置 RTC 时间。
 *        注意：VBAT 域由备份电池供电，只要电池有电，该值在系统断电后仍保留。
 */
#define BKP_VALUE 0x32F0

/* ============================ 内部数据类型 ============================ */

/**
 * @brief  复位源类型枚举（硬件层内部使用，不暴露给 Service 层）
 *         用于 RTC_PrintResetSource() 内部判断复位类型
 */
typedef enum {
    RTC_RESET_NONE      = 0,   /**< 无复位标志（不应出现，仅供防御） */
    RTC_RESET_POWER_ON  = 1,   /**< 上电复位（PORRST） */
    RTC_RESET_EXTERNAL  = 2,   /**< 外部引脚复位（EPRST） */
} rtc_reset_source_t;

/* ============================ 内部变量 ============================ */

/**
 * @brief RTC 初始化参数结构体（由标准库定义）
 *        硬件层内部持有，不暴露给服务层
 *        包含日期、时间、显示格式、预分频器等完整 RTC 配置
 */
static rtc_parameter_struct s_rtc_initpara;

/**
 * @brief RTC 闹钟参数结构体（由标准库定义）
 *        硬件层内部持有，不暴露给服务层
 */
static rtc_alarm_struct s_rtc_alarm;

/**
 * @brief 异步预分频器值（7-bit，范围 0x00~0x7F）
 *        对 RTC 时钟源进行第一级分频，得到 ck_apre
 *        实际分频比 = 值 + 1
 */
static uint32_t s_prescaler_a = 0;

/**
 * @brief 同步预分频器值（15-bit，范围 0x00~0x7FFF）
 *        对 ck_apre 进行第二级分频，得到 ck_spre = 1Hz
 *        实际分频比 = 值 + 1
 */
static uint32_t s_prescaler_s = 0;

/* ============================ 内部函数 ============================ */

/*!
    \brief      检查 RTC 时钟源是否已配置（内部函数）
    \param[in]  none
    \param[out] none
    \retval     0 = 未配置，1 = 已配置

    \details
    读取 RCU_BDCTL 寄存器的 RTCSRC[1:0] 位段（bit[9:8]）：
    - 0b00 = 无时钟源 → RTC 未配置
    - 0b01 = LXTAL
    - 0b10 = IRC32K
    - 0b11 = HXTAL/32
    GET_BITS(RCU_BDCTL, 8, 9) 提取 bit[9:8]，非 0 即为已配置。
*/
static uint8_t RTC_ClockSourceIsConfigured(void)
{
    return (GET_BITS(RCU_BDCTL, 8, 9) != 0) ? 1U : 0U;
}

/*!
    \brief      获取最近一次复位源类型（内部函数）
    \param[in]  none
    \param[out] none
    \retval     rtc_reset_source_t 枚举值

    \details
    优先检测上电复位（PORRST），再检测外部复位（EPRST）。
    两者均未置位时返回 RTC_RESET_NONE（理论上不应出现）。
*/
static rtc_reset_source_t RTC_GetResetSource(void)
{
    if (RESET != rcu_flag_get(RCU_FLAG_PORRST))
    {
        return RTC_RESET_POWER_ON;
    }
    if (RESET != rcu_flag_get(RCU_FLAG_EPRST))
    {
        return RTC_RESET_EXTERNAL;
    }
    return RTC_RESET_NONE;
}

/* ============================ 硬件层接口实现 ============================ */

/*!
    \brief      RTC 硬件完整初始化
    \param[in]  none
    \param[out] none
    \retval     none

    \details
    执行流程（按顺序）：

    [阶段1] 备份域使能
    - rcu_periph_clock_enable(RCU_PMU)：使能电源管理单元时钟
      原因：RTC 寄存器位于备份域，受电源管理单元（PMU）保护
    - pmu_backup_write_enable()：解除备份域写保护
      原因：复位后备份域默认写保护，必须先解锁才能写 RTC 寄存器

    [阶段2] 时钟源使能与等待稳定
    - 根据 RTC_CLOCK_SOURCE_xxx 宏，调用 rcu_osci_on() 使能对应振荡器
    - 调用 rcu_osci_stab_wait() 阻塞等待振荡器输出稳定
      LXTAL 起振较慢（0.5~2s），IRC32K 几乎立即就绪

    [阶段3] 时钟源切换
    - 调用 rcu_rtc_clock_config() 写 RCU_BDCTL 的 RTCSRC[1:0] 位段

    [阶段4] 预分频器计算
    - RTC 内部两级分频器：异步（粗分频）→ 同步（细分频）→ ck_spre = 1Hz
    - 公式：f_ck_spre = f_rtc_clock / (prescaler_a + 1) / (prescaler_s + 1)
    - LXTAL 32768Hz: 32768 / 128 / 256 = 1Hz → prescaler_a=0x7F, prescaler_s=0xFF
    - IRC32K ~32000Hz: 32000 / 100 / 320 = 1Hz → prescaler_a=0x63, prescaler_s=0x13F

    [阶段5] 外设使能与同步
    - rcu_periph_clock_enable(RCU_RTC)：使能 RTC 外设总线时钟
    - rtc_register_sync_wait()：等待影子寄存器与备份域同步完成
*/
void RTC_HwInit(void)
{
    /* === 阶段1：备份域使能 === */
    /* 使能 PMU 时钟 —— PMU 控制备份域访问权限 */
    rcu_periph_clock_enable(RCU_PMU);
    /* 解除备份域写保护 —— 否则 RTC 寄存器写入会被硬件忽略 */
    pmu_backup_write_enable();

    /* === 阶段2-3：根据时钟源宏使能振荡器、等待稳定、切换时钟源 === */
#if defined(RTC_CLOCK_SOURCE_IRC32K)
    /* 使能内部 32K RC 振荡器 */
    rcu_osci_on(RCU_IRC32K);
    /* 阻塞等待振荡器输出稳定（IRC32K 起振极快，通常微秒级） */
    rcu_osci_stab_wait(RCU_IRC32K);
    /* 将 RTC 时钟源设置为 IRC32K */
    rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);

    /* IRC32K 模式下，异步分频比=100，同步分频比=320 */
    s_prescaler_s = 0x13F;  /* 319 → 分频比 = 319+1 = 320 */
    s_prescaler_a = 0x63;   /* 99  → 分频比 = 99+1  = 100 */

#elif defined(RTC_CLOCK_SOURCE_LXTAL)
    /* 使能外部 32.768K 低速晶体振荡器 */
    rcu_osci_on(RCU_LXTAL);
    /* 阻塞等待晶体起振并稳定（LXTAL 起振较慢，通常 0.5~2 秒） */
    rcu_osci_stab_wait(RCU_LXTAL);
    /* 将 RTC 时钟源设置为 LXTAL */
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

    /* LXTAL 模式下，异步分频比=128，同步分频比=256 */
    s_prescaler_s = 0xFF;   /* 255 → 分频比 = 255+1 = 256 */
    s_prescaler_a = 0x7F;   /* 127 → 分频比 = 127+1 = 128 */

#else
#error RTC clock source should be defined.
#endif

    /* === 阶段5：使能 RTC 外设时钟，等待寄存器同步 === */
    rcu_periph_clock_enable(RCU_RTC);
    /* 等待 RTC 影子寄存器从备份域同步到 APB 总线域 */
    rtc_register_sync_wait();
}

/*!
    \brief      检查 RTC 是否已配置过
    \param[in]  none
    \param[out] none
    \retval     0 = 未配置（首次上电或 VBAT 掉电），1 = 已配置

    \details
    组合两个条件判断 RTC 是否已完成初始配置：
    1. 备份寄存器 BKP0 是否包含魔术字 BKP_VALUE
       - BKP_VALUE 匹配 → VBAT 域数据完好，RTC 时间未丢失
       - BKP_VALUE 不匹配 → 首次上电或备份电池耗尽
    2. RTC 时钟源位段是否非零
       - 非零 → 时钟源已被配置过
       - 全零 → 从未配置过时钟源
    任一条件不满足即返回 0（未配置）
*/
uint8_t RTC_IsConfigured(void)
{
    /* 条件1：备份寄存器魔术字比对 */
    if (RTC_BKP0 != BKP_VALUE)
    {
        return 0U;
    }
    /* 条件2：时钟源位段检查 */
    if (0U == RTC_ClockSourceIsConfigured())
    {
        return 0U;
    }
    return 1U;
}

/*!
    \brief      标记 RTC 为"已配置"状态
    \param[in]  none
    \param[out] none
    \retval     none

    \details
    将魔术字 BKP_VALUE 写入备份寄存器 BKP0。
    下次上电时 RTC_IsConfigured() 读取 BKP0，
    若值 == BKP_VALUE → 说明 VBAT 域数据完好 → 跳过配置流程。
    前提条件：备份电池持续供电，否则 BKP0 会在断电后被清零。
*/
void RTC_SetConfigured(void)
{
    RTC_BKP0 = BKP_VALUE;
}

/*!
    \brief      设置 RTC 时间
    \param[in]  time 指向 rtc_time_t 结构体，所有字段为 BCD 编码
    \param[out] none
    \retval     0 = 失败，1 = 成功

    \details
    填充 s_rtc_initpara 结构体并调用标准库 rtc_init()：
    1. 预分频值由 RTC_HwInit() 已计算好（s_prescaler_a / s_prescaler_s）
    2. 日期时间字段由 rtc_time_t 入参填入
    3. 星期字段固定为 RTC_SATURDAY（简化处理，不影响计时功能）
    4. 显示格式固定为 24 小时制
    5. 调用 rtc_init() 将参数写入 RTC 寄存器并启动计时
*/
uint8_t RTC_SetTime(const rtc_time_t *time)
{
    /* === 填充预分频值（由 RTC_HwInit() 已计算） === */
    s_rtc_initpara.factor_asyn = s_prescaler_a;
    s_rtc_initpara.factor_syn  = s_prescaler_s;

    /* === 填充日期时间字段 === */
    s_rtc_initpara.year        = time->year;
    s_rtc_initpara.month       = time->month;
    s_rtc_initpara.date        = time->day;
    s_rtc_initpara.hour        = time->hour;
    s_rtc_initpara.minute      = time->minute;
    s_rtc_initpara.second      = time->second;

    /* === 填充固定字段 === */
    s_rtc_initpara.day_of_week    = RTC_SATURDAY;  /* 星期固定为周六（简化） */
    s_rtc_initpara.display_format = RTC_24HOUR;     /* 24 小时制 */
    s_rtc_initpara.am_pm          = RTC_AM;         /* 24 小时制下无意义 */

    /* === 调用标准库 API 将时间参数写入 RTC 寄存器 === */
    if (ERROR == rtc_init(&s_rtc_initpara))
    {
        return 0U;  /* 失败 */
    }
    return 1U;      /* 成功 */
}

/*!
    \brief      获取 RTC 当前时间
    \param[in]  none
    \param[out] time 指向 rtc_time_t 结构体，函数填充 BCD 编码值
    \retval     none

    \details
    调用 rtc_current_time_get() 从 RTC 时间/日期寄存器的影子副本中读出当前时间。
    影子寄存器机制确保读操作原子性，避免读到正在跳变的中间值。
*/
void RTC_GetTime(rtc_time_t *time)
{
    /* 从 RTC 寄存器读取当前时间到内部结构体 */
    rtc_current_time_get(&s_rtc_initpara);

    /* 通过出参返回各字段（BCD 格式） */
    time->year   = (uint8_t)s_rtc_initpara.year;
    time->month  = (uint8_t)s_rtc_initpara.month;
    time->day    = (uint8_t)s_rtc_initpara.date;
    time->hour   = (uint8_t)s_rtc_initpara.hour;
    time->minute = (uint8_t)s_rtc_initpara.minute;
    time->second = (uint8_t)s_rtc_initpara.second;
}

/*!
    \brief      获取 RTC ALARM0 闹钟设定值
    \param[in]  none
    \param[out] time 指向 rtc_time_t 结构体，函数填充时分秒字段
    \retval     none

    \details
    调用 rtc_alarm_get(RTC_ALARM0, &s_rtc_alarm) 从闹钟寄存器组
    （RTC_ALRM0TD 闹钟时间日期寄存器 + RTC_ALRM0SS 闹钟亚秒寄存器）
    中读取 ALARM0 的设定值。
    year/month/day 字段填充为 0（闹钟不涉及日期字段）。
*/
void RTC_GetAlarm(rtc_time_t *time)
{
    /* 从 RTC_ALRM0TD 和 RTC_ALRM0SS 寄存器读取闹钟值 */
    rtc_alarm_get(RTC_ALARM0, &s_rtc_alarm);

    /* 闹钟只涉及时分秒，日期字段置零 */
    time->year   = 0;
    time->month  = 0;
    time->day    = 0;
    time->hour   = (uint8_t)s_rtc_alarm.alarm_hour;
    time->minute = (uint8_t)s_rtc_alarm.alarm_minute;
    time->second = (uint8_t)s_rtc_alarm.alarm_second;
}

/*!
    \brief      检测并打印复位源信息
    \param[in]  none
    \param[out] none
    \retval     none

    \details
    执行流程：
    1. 读取 RCU 复位标志（PORRST / EPRST）
    2. 通过 printf 打印复位类型（调试用途）
    3. 清除所有复位标志，避免影响后续上电的复位源判断

    复位源说明：
    - PORRST（Power-On Reset）：上电复位，电源从无到有
    - EPRST（External Pin Reset）：外部引脚复位（NRST 引脚低电平）
*/
void RTC_PrintResetSource(void)
{
    rtc_reset_source_t reset_src = RTC_GetResetSource();

    if (RTC_RESET_POWER_ON == reset_src)
    {
        printf("power on reset occurred....\n\r");
    }
    else if (RTC_RESET_EXTERNAL == reset_src)
    {
        printf("external reset occurred....\n\r");
    }

    /* 清除所有复位标志，避免本次复位标志影响后续判断 */
    rcu_all_reset_flag_clear();
}
