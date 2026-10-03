#ifndef __AUTO_REPORT_SERVICE_H
#define __AUTO_REPORT_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  初始化自动上报业务状态机。
 * @return void 无返回值。
 *
 * 自动上报默认处于停止状态。该函数只复位内存态，不主动发送任何 RS485 帧，
 * 保证 APP 上电后不会污染评测串口。
 */
void AutoReport_Service_Init(void);

/**
 * @brief  启动定时自动上报，并立即发送首帧 0x0302 响应数据。
 * @return void 无返回值。
 *
 * 首帧内容严格按赛题定义组织为 12 字节：
 * 4 字节 RTC Unix 时间戳 + 4 字节 CH0 IEEE754 大端 float + 4 字节 CH1 IEEE754 大端 float。
 * CH2/PT100 相关字段已按当前任务计划延后到最终阶段处理。
 */
void AutoReport_Service_Start(void);

/**
 * @brief  停止定时自动上报。
 * @return void 无返回值。
 *
 * 停止动作只关闭本状态机和采集指示灯，0x0303 的 OK 应答由 RS485 命令处理层发送。
 */
void AutoReport_Service_Stop(void);

/**
 * @brief  自动上报周期任务，由主循环时间片调度器调用。
 * @return void 无返回值。
 *
 * 该任务不阻塞等待时间，而是比较系统毫秒 tick 与下一次到期时间。
 * 到期后发送一帧与 0x0302 首帧相同格式的数据，并把下一次到期点推进到当前时间之后。
 */
void AutoReport_Service_Task(void);

/**
 * @brief  查询自动上报状态。
 * @return 1 表示正在自动上报，0 表示空闲。
 */
uint8_t AutoReport_Service_IsRunning(void);

#endif
