#include "Sleep_service.h"
#include "Rs485_service.h"
#include "AutoReport_service.h"
#include "Oled_service.h"
#include "OLED_IIC.h"

#define SLEEP_SERVICE_WAKEUP_DELAY_SECONDS 10U
#define SLEEP_SERVICE_ENTER_DELAY_MS       20UL
#define SLEEP_SERVICE_WAKEUP_TEXT          "instrument wakeup"

static volatile uint8_t s_alarm_wakeup_flag = 0U;
static uint8_t s_sleep_requested = 0U;
static uint32_t s_enter_after_ms = 0UL;

static uint8_t Sleep_Service_BcdToDec(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static uint8_t Sleep_Service_DecToBcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

/**
 * @brief  配置 RTC ALARM0 为当前 RTC 秒值之后 10 秒触发。
 * @return 1 表示配置成功，0 表示 RTC Alarm 禁用或配置失败。
 *
 * 这里只改 ALARM0，不重新初始化 RTC，也不重置备份域。
 * 赛题前序已经依赖 RTC 时间查询和设置，如果照搬模板重置备份域，
 * 会把已设置时间和持久状态一起破坏。Alarm 使用“只匹配秒”的方式：
 * hour/minute/date 全部 mask，目标秒 = 当前秒 + 10 后取模。
 */
static uint8_t Sleep_Service_ConfigRtcAlarmAfter10s(void)
{
    rtc_time_t now;
    rtc_alarm_struct alarm;
    uint8_t second_dec;
    uint8_t alarm_second_dec;

    RTC_GetTime(&now);
    second_dec = Sleep_Service_BcdToDec(now.second);
    if (second_dec >= 60U)
    {
        return 0U;
    }

    alarm_second_dec = (uint8_t)((second_dec + SLEEP_SERVICE_WAKEUP_DELAY_SECONDS) % 60U);

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    if (rtc_alarm_disable(RTC_ALARM0) == ERROR)
    {
        return 0U;
    }

    alarm.alarm_mask = RTC_ALARM_DATE_MASK | RTC_ALARM_HOUR_MASK | RTC_ALARM_MINUTE_MASK;
    alarm.weekday_or_date = RTC_ALARM_DATE_SELECTED;
    alarm.alarm_day = 0x01U;
    alarm.alarm_hour = 0x00U;
    alarm.alarm_minute = 0x00U;
    alarm.alarm_second = Sleep_Service_DecToBcd(alarm_second_dec);
    alarm.am_pm = RTC_AM;

    rtc_flag_clear(RTC_FLAG_ALRM0);
    exti_interrupt_flag_clear(EXTI_17);
    rtc_alarm_config(RTC_ALARM0, &alarm);
    rtc_interrupt_enable(RTC_INT_ALARM0);
    exti_init(EXTI_17, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_Alarm_IRQn, 2U, 1U);
    rtc_alarm_enable(RTC_ALARM0);

    return 1U;
}

/**
 * @brief  从 deep sleep 返回后恢复系统时钟、SysTick 与业务外设显示状态。
 * @return void
 *
 * GD32F4 从 deep sleep 唤醒后系统时钟会回到内部 IRC16M。
 * 若不恢复 PLL 和 SystemCoreClock，后续 SysTick、USART 波特率等都会偏移。
 */
static void Sleep_Service_RestoreAfterWakeup(void)
{
    rtc_alarm_disable(RTC_ALARM0);
    rtc_interrupt_disable(RTC_INT_ALARM0);
    rtc_flag_clear(RTC_FLAG_ALRM0);
    exti_interrupt_flag_clear(EXTI_17);
    SystemInit();
    SystemCoreClockUpdate();
    systick_config();
    Rs485_Service_Init();
    Oled_Service_Init();
}

static void Sleep_Service_EnterDeepSleep(void)
{
    AutoReport_Service_Stop();
    led_state[0] = 0U;

    if (!Sleep_Service_ConfigRtcAlarmAfter10s())
    {
        return;
    }

    OLED_Display_Off();

    s_alarm_wakeup_flag = 0U;
    rcu_periph_clock_enable(RCU_PMU);
    __DSB();
    __ISB();
    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);
    __DSB();
    __ISB();

    Sleep_Service_RestoreAfterWakeup();

    if (s_alarm_wakeup_flag != 0U)
    {
        Rs485_Service_SendRawText(SLEEP_SERVICE_WAKEUP_TEXT);
    }
}

void Sleep_Service_Init(void)
{
    s_alarm_wakeup_flag = 0U;
    s_sleep_requested = 0U;
    s_enter_after_ms = 0UL;
}

void Sleep_Service_RequestRtcWakeup(void)
{
    s_sleep_requested = 1U;
    s_enter_after_ms = get_millis() + SLEEP_SERVICE_ENTER_DELAY_MS;
}

void Sleep_Service_Task(void)
{
    if (s_sleep_requested == 0U)
    {
        return;
    }

    if ((uint32_t)(get_millis() - s_enter_after_ms) >= 0x80000000UL)
    {
        return;
    }

    s_sleep_requested = 0U;
    Sleep_Service_EnterDeepSleep();
}

void Sleep_Service_RtcAlarmIrqHandler(void)
{
    /* RTC_Alarm_IRQHandler owns the vector entry; this hook owns only the service state. */
    if (rtc_flag_get(RTC_FLAG_ALRM0) != RESET)
    {
        rtc_flag_clear(RTC_FLAG_ALRM0);
        exti_interrupt_flag_clear(EXTI_17);
        s_alarm_wakeup_flag = 1U;
    }
}
