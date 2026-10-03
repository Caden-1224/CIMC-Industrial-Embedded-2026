/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：Rtc_service.h
 * 作者: Caden
 * 平台: 2025CIMC IHD-V04
 * 版本: Caden        2026/5/10    V0.04    分层重构：纯 RTC 业务逻辑
 *
 * 说明：RTC 服务层头文件。
 *       本层负责 RTC 初始化流程编排、周期健康检测、
 *       当前时间/闹钟显示等业务逻辑。
 *       本层通过 RTC.h 声明的硬件抽象层接口访问 RTC 硬件，
 *       禁止直接操作寄存器或标准库结构体。
 *       本层不涉及 LED 控制、串口输入等 UI 逻辑。
 ************************************************************/

#ifndef __RTC_SERVICE_H
#define __RTC_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  RTC 服务初始化 —— 完整初始化流程编排
 * @note   调用时机：System_Init() 中
 *         执行流程：
 *         1. 调用硬件层 RTC_HwInit() 完成时钟源和预分频器初始化
 *         2. 调用 RTC_IsConfigured() 检查是否已配置
 *         3. 若未配置 → 上层负责处理首次配置
 *         4. 若已配置 → 检测复位源并显示当前时间
 *         5. 初始化健康检测变量
 */
void RTC_Service_Init(void);

/**
 * @brief  RTC 周期任务 —— 由时间片调度器调用
 * @note   最小健康检测：读取当前秒值，与上次比较
 *         - 秒值变化 → RTC 正常走时 → rtc_healthy = 1
 *         - 连续 RTC_STALE_THRESHOLD 次秒值不变 → RTC 停走 → rtc_healthy = 0
 *         外部模块读取 rtc_healthy 控制 LED 等状态指示
 */
void RTC_Service_Task(void);

/**
 * @brief  在串口控制台显示当前 RTC 时间
 *         格式：Current time: 20YY-MM-DD : HH:MM:SS
 */
void RTC_Service_ShowTime(void);

/**
 * @brief  在串口控制台显示当前 RTC 闹钟设定值
 *         格式：The alarm: HH:MM:SS
 */
void RTC_Service_ShowAlarm(void);

/**
 * @brief  RTC 健康状态（只读）
 *         0 = 未检测 / 异常（RTC 停走或未初始化）
 *         1 = 正常（秒值在持续递增）
 * @note   由 RTC_Service_Task() 周期更新，供 LED 等模块读取
 */
extern uint8_t rtc_healthy;

#endif
