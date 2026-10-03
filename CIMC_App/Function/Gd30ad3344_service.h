/************************************************************
 * Copyright: 2025 CIMC
 * File     : Gd30ad3344_service.h
 * Brief    : GD30AD3344 外部 ADC 的服务层接口。
 *
 * Layering:
 *   - HardWare/GD30AD3344 负责 SPI3、DMA、片选和 ADC 配置字收发。
 *   - 本文件所在的 Function 层只负责“采哪一路、如何换算、结果放哪”。
 *
 * Usage:
 *   - System_Init() 调用 Gd30ad3344_Service_Init() 完成外部 ADC 初始化。
 *   - 轮询调度器周期调用 Gd30ad3344_Service_Task() 刷新采样结果。
 *   - 其他业务模块直接读取 gd30ad3344_result，避免再包一层无必要的 getter。
 ************************************************************/
#ifndef __GD30AD3344_SERVICE_H
#define __GD30AD3344_SERVICE_H

#include "HeaderFiles.h"

typedef struct
{
    /**
     * @brief GD30AD3344 当前采样电压，单位 V。
     *
     * @details 该值来自底层 GD30AD3344_AD_Read() 的换算结果，量程由
     *          Gd30ad3344_service.c 中的 GD30AD3344_SERVICE_RANGE 决定。
     */
    float voltage;

    /**
     * @brief 根据 PT100 前端电路换算出来的电阻值，单位 ohm。
     *
     * @details 当前公式为：
     *          resistance = voltage / INA增益 / RTD激励电流(mA) * 1000。
     *          乘以 1000 是因为激励电流配置值用 mA 表示，需要换算成 A。
     */
    float resistance;

    /**
     * @brief 外部 ADC 服务是否处于可用状态。
     *
     * @details 1 表示底层驱动认为 GD30AD3344 已初始化且最近一次访问未超时；
     *          0 表示尚未初始化或 SPI/DMA 访问失败。
     */
    uint8_t ready;
} Gd30ad3344_Result_t;

/**
 * @brief 外部 GD30AD3344/PT100 的最新服务层结果。
 *
 * @details 这是服务层对上提供的唯一状态出口。OLED、USART、RS485 或
 *          其他业务模块如需显示/上报外部 ADC 数据，直接读取这个结构体即可。
 */
extern Gd30ad3344_Result_t gd30ad3344_result;

/**
 * @brief 初始化 GD30AD3344 服务。
 *
 * @details 内部会调用底层 GD30AD3344_Init()，配置 SPI3、DMA、GPIO 和 ADC
 *          默认配置字；随后立即采样一次，让 gd30ad3344_result 在进入主循环前
 *          就有一份初值。
 */
void Gd30ad3344_Service_Init(void);

/**
 * @brief GD30AD3344 服务周期任务。
 *
 * @details 该函数由 Function.c 的时间片轮询调度器调用。它不阻塞等待转换完成，
 *          只按当前底层驱动的同步 SPI/DMA 读数流程刷新一次电压、电阻和 ready 状态。
 */
void Gd30ad3344_Service_Task(void);

#endif /* __GD30AD3344_SERVICE_H */
