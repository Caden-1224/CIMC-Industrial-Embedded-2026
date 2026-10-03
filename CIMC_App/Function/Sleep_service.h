#ifndef __SLEEP_SERVICE_H
#define __SLEEP_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  初始化睡眠唤醒服务的运行态。
 * @return void
 *
 * 本函数只清理内存状态，不主动配置 RTC Alarm，也不发送串口内容。
 * 赛题要求的低功耗入口由 `Sleep_Service_RequestRtcWakeup()` 显式触发。
 */
void Sleep_Service_Init(void);

/**
 * @brief  请求进入 RTC 10 秒自动唤醒流程。
 * @return void
 *
 * RS485 命令层在发送 `0x03AA` 的 OK 应答后调用本函数。
 * 实际进入深度睡眠的动作延后到 `Sleep_Service_Task()` 中完成，
 * 这样可以让命令处理栈先退出，避免在协议分发函数内部直接切换功耗状态。
 */
void Sleep_Service_RequestRtcWakeup(void);

/**
 * @brief  睡眠服务周期任务，由主循环时间片调度器调用。
 * @return void
 *
 * 当检测到待处理睡眠请求时，配置 RTC ALARM0 为 10 秒后触发，
 * 然后进入 MCU deep sleep；RTC Alarm 唤醒后恢复时钟和必要外设，
 * 最后按赛题要求直发 `instrument wakeup` 字符串。
 */
void Sleep_Service_Task(void);

/**
 * @brief  RTC Alarm 中断钩子，由 `RTC_Alarm_IRQHandler()` 调用。
 * @return void
 *
 * ISR 只做最小清标志和置位动作，不发送串口、不执行耗时恢复。
 * 唤醒后的完整恢复流程在 `pmu_to_deepsleepmode()` 返回后由服务层继续完成。
 */
void Sleep_Service_RtcAlarmIrqHandler(void);

#endif
