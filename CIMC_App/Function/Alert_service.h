#ifndef __ALERT_SERVICE_H
#define __ALERT_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  初始化 CH0/CH1 告警服务并从 Flash 恢复最近 10 条记录。
 * @return void 无返回值。
 *
 * 本服务只负责 T08 的 CH0/CH1 告警闭环。CH2/PT100 告警依赖外部硬件，
 * 已按任务计划后移到 T16，不在这里参与阈值判断。
 */
void Alert_Service_Init(void);

/**
 * @brief  告警周期任务，检查 CH0/CH1 是否从未超阈进入超阈状态。
 * @return void 无返回值。
 *
 * 任务采用上升沿记录策略：同一通道持续高于阈值时只记录一次，
 * 待采样值回到阈值以下后，下一次再次超阈才生成新告警。
 */
void Alert_Service_Task(void);

/**
 * @brief  查询最近 10 条告警记录并通过 RS485 直接发送 ASCII 文本。
 * @return void 无返回值。
 *
 * 有记录时按时间倒序逐行输出；无记录时输出原始字符串 "empty"。
 * 查询响应不走赛题协议帧封装。
 */
void Alert_Service_SendHistoryText(void);

/**
 * @brief  清除 Flash 中保存的告警记录。
 * @return 1 表示清除成功，0 表示清除失败。
 */
uint8_t Alert_Service_Clear(void);

#endif
