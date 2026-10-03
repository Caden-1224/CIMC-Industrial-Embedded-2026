/************************************************************
 * 文件：gd32f4xx_it.c
 * 平台: GD32F470VE
 * 说明: 全局中断服务函数入口
 ************************************************************/
/* ================== 头文件 ================== */
#include "gd32f4xx_it.h"
#include "systick.h"
#include "Sleep_service.h"
/* ==========================================================
 *                        系统异常处理
 *          以下为 Cortex-M4 内核固定异常，一般无需修改
 * ========================================================== */
/**
 * @brief  不可屏蔽中断
 */
void NMI_Handler(void)
{
}

/**
 * @brief  硬件错误（进入死循环等待调试器Attached）
 */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief  内存管理错误
 */
void MemManage_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief  总线错误
 */
void BusFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief  用法错误
 */
void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief  超级调用（SVCall）
 */
void SVC_Handler(void)
{
}

/**
 * @brief  调试监控
 */
void DebugMon_Handler(void)
{
}

/**
 * @brief  可挂起系统调用
 */
void PendSV_Handler(void)
{
}

/* ==========================================================
 *                     用户自定义中断处理
 *         在此区域添加外设中断服务函数（如串口、定时器、DMA）
 * ========================================================== */

/**
 * @brief  系统滴答定时器中断（每 1ms 触发一次）
 * @note   delay_decrement()  驱动阻塞延时
 *         tick_increment()   驱动全局毫秒计数器（时间片轮询依赖）
 */
void SysTick_Handler(void)
{
    delay_decrement();
    tick_increment();
}

/**
 * @brief  RTC ALARM0 中断入口。
 * @note   当前仅服务 T09 睡眠唤醒；具体清标志与状态置位交给 Sleep_service，
 *         保持中断文件只承担向量转发职责。
 */
void RTC_Alarm_IRQHandler(void)
{
    Sleep_Service_RtcAlarmIrqHandler();
}
