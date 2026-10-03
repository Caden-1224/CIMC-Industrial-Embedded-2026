#ifndef __ADC_H
#define __ADC_H

#include "HeaderFiles.h"

/**
 * @brief 内部 ADC 业务通道数量。
 * @details 赛题 CH0 使用 PC0/ADC_CHANNEL_10，CH1 使用 PC1/ADC_CHANNEL_11。
 */
#define ADC_INTERNAL_CHANNEL_COUNT 2U

/**
 * @brief CH0 的业务索引。
 * @details CH0 对应板载电位器，服务层用该索引选择 ADC_CHANNEL_10。
 */
#define ADC_INTERNAL_CH0_INDEX 0U

/**
 * @brief CH1 的业务索引。
 * @details CH1 对应 DAC 回读，PA4 跳接到 PC1 后，服务层用该索引选择 ADC_CHANNEL_11。
 */
#define ADC_INTERNAL_CH1_INDEX 1U

/**
 * @brief 初始化 ADC GPIO、ADC0 时钟和基础转换参数。
 * @return void 无返回值。
 */
void ADC_port_init(void);

/**
 * @brief 配置 ADC0 为主动单次采样模式。
 * @return void 无返回值。
 */
void ADC_Init(void);

/**
 * @brief 主动采样一个内部 ADC0 规则通道。
 * @param adc_channel GD32 标准库通道号，例如 ADC_CHANNEL_10 表示 PC0。
 * @return uint16_t 12 位原始 ADC 码，范围 0..4095。
 */
uint16_t ADC_ReadChannelRaw(uint8_t adc_channel);

#endif
