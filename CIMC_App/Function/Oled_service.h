#ifndef __OLED_SERVICE_H
#define __OLED_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief 初始化 OLED 并显示首帧状态页。
 */
void Oled_Service_Init(void);

/**
 * @brief 周期刷新 OLED 评分状态页。
 *
 * @details 第一行用 16px 字体显示队伍编号 `2026397013`；第二行用 16px 字体
 *          在自动上报期间显示 `AutoSample`，其余 APP 运行时间显示 `IDLE`。
 */
void Oled_Service_Task(void);

#endif
