#include "TIMER_BASIC.h"

/***********************************************************************
 * @brief      初始化 TIM6 基本定时器（翻转 LED3）
 * @details    配置 TIM6 工作在向上计数、边沿对齐模式，通过 PSC 分频
 *              和 ARR 自动重装载值实现约 1Hz 的定时更新中断，驱动
 *              LED3 以 0.5Hz 频率翻转。
 *
 *              时钟链：APB1(60MHz) → 4倍频 → 240MHz TIM6CLK
 *              → PSC(2400分频) → 100kHz计数频率
 *              → ARR(50000) → 计数周期 ≈ 0.6s → 更新中断 ≈ 1Hz
 *
 * @param      none
 * @return     none
 ***********************************************************************/
void Timer_Basic_Init(void)
{
    /* ---------------------------------------------------------------
     * Step 1: 配置 NVIC（嵌套向量中断控制器）
     * --------------------------------------------------------------- */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0); // 4位抢占优先级，0位子优先级
    nvic_irq_enable(TIMER6_IRQn, 2, 0);               // 使能TIM6_IRQn，抢占优先级2，子优先级0

    /* ---------------------------------------------------------------
     * Step 2: 配置 TIM6 时钟源倍频
     * --------------------------------------------------------------- */
    // 在当前 240MHz SYSCLK 配置下，APB1 = AHB / 4 = 60MHz。
    // RCU_TIMER_PSC_MUL4 会让 APB1 域定时器时钟恢复到 AHB，即 TIM6CLK = 240MHz。
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL4);

    /* ---------------------------------------------------------------
     * Step 3: 使能 TIM6 外设时钟并复位
     * --------------------------------------------------------------- */
    rcu_periph_clock_enable(RCU_TIMER6); // 使能 TIM6 时钟
    timer_deinit(TIMER6);                // 复位 TIM6 至默认状态

    /* ---------------------------------------------------------------
     * Step 4: 配置 TIM6 初始化参数
     * --------------------------------------------------------------- */
    timer_parameter_struct timer6_initpara;

    // 预分频值：TIM6CLK / (PSC + 1) = 240MHz / 2400 = 100kHz
    timer6_initpara.prescaler = 2400 - 1;

    // 计数对齐模式：边沿对齐（EDGE-aligned），在中央对齐模式下会产生额外中断
    timer6_initpara.alignedmode = TIMER_COUNTER_EDGE;

    // 计数方向：向上计数（从 0 递增至 ARR 后溢出）
    timer6_initpara.counterdirection = TIMER_COUNTER_UP;

    // 自动重装载值：计数器达到此值时溢出并触发更新中断
    // 计数周期 = (ARR + 1) / 100kHz = 50000 / 100kHz = 0.5s
    timer6_initpara.period = 50000 - 1;

    // 时钟分频：用于死区生成等高级功能，此处不分频
    timer6_initpara.clockdivision = TIMER_CKDIV_DIV1;

    // 重复计数器：用于高级定时器，此处不使用
    timer6_initpara.repetitioncounter = 0;

    // 初始化 TIM6
    timer_init(TIMER6, &timer6_initpara);

    /* ---------------------------------------------------------------
     * Step 5: 配置更新中断并清除标志
     * --------------------------------------------------------------- */
    timer_interrupt_enable(TIMER6, TIMER_INT_UP);     // 使能更新中断
    timer_interrupt_flag_clear(TIMER6, TIMER_INT_UP); // 清除初始标志避免上电误触发

    /* ---------------------------------------------------------------
     * Step 6: 启动 TIM6 计数器
     * --------------------------------------------------------------- */
    timer_enable(TIMER6);
}

/***********************************************************************
 * @brief      TIM6 更新中断服务函数（ISR）
 * @details    每当 TIM6 计数器溢出时触发，执行 LED3 状态翻转，实现
 *              约 1Hz 的 LED 闪烁（每 0.5s 翻转一次 LED）
 *
 * @note       中断服务函数必须保持简短，仅做状态切换标记
 * @param      none
 * @return     none
 ***********************************************************************/
void TIMER6_IRQHandler(void)
{
    // 检查更新中断标志（每计数周期结束时会置位）
    if (timer_interrupt_flag_get(TIMER6, TIMER_INT_UP) == SET)
    {
        // 清除更新中断标志，为下一次中断做准备
        timer_interrupt_flag_clear(TIMER6, TIMER_INT_UP);

    }
}
