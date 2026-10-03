#ifndef __APP_PARAM_SERVICE_H
#define __APP_PARAM_SERVICE_H

#include "HeaderFiles.h"

#define APP_PARAM_FIRMWARE_VERSION_MAJOR 0x02U
#define APP_PARAM_FIRMWARE_VERSION_MINOR 0x00U
#define APP_PARAM_FIRMWARE_VERSION_PATCH 0x01U
#define APP_PARAM_FIRMWARE_VERSION_BUILD 0x00U

#define APP_PARAM_DEFAULT_DEVICE_ID      0x0001U
#define APP_PARAM_DEFAULT_BAUD_CODE      0x13U
#define APP_PARAM_DEFAULT_REPORT_CODE    0x01U
#define APP_PARAM_DEFAULT_ALARM_MODE     0x02U

typedef enum
{
    APP_PARAM_CHANNEL_CH0 = 0,
    APP_PARAM_CHANNEL_CH1 = 1,
    APP_PARAM_CHANNEL_CH2 = 2
} app_param_channel_t;

/**
 * @brief  初始化 APP 业务参数服务，并从 Bootloader 共享参数页恢复配置。
 * @return void 无返回值。
 *
 * 本服务只管理赛题 APP 层业务参数，不接管 Bootloader 的升级状态机字段。
 * 当前 T03 阶段先覆盖设备 ID 与 RS485 波特率；后续变比、阈值、上报间隔、
 * 告警配置会继续追加到同一参数块，避免业务状态散落在各个服务里。
 */
void AppParam_Service_Init(void);

/**
 * @brief  获取当前设备 ID。
 * @return 当前生效的设备 ID，范围为 `0x0001..0xFFFE`。
 */
uint16_t AppParam_Service_GetDeviceId(void);

/**
 * @brief  设置并持久化设备 ID。
 * @param  device_id 赛题设备 ID，必须位于 `0x0001..0xFFFE`，`0xFFFF` 为广播地址不可作为本机 ID。
 * @return 1 表示参数已写入 Flash；0 表示入参非法或写入失败。
 */
uint8_t AppParam_Service_SetDeviceId(uint16_t device_id);

/**
 * @brief  获取当前波特率编码。
 * @return 赛题定义的波特率编码：`0x11=4800`、`0x12=9600`、`0x13=19200`、`0x14=115200`。
 */
uint8_t AppParam_Service_GetBaudCode(void);

/**
 * @brief  设置并持久化波特率编码。
 * @param  baud_code 赛题定义的波特率编码。
 * @return 1 表示参数已写入 Flash；0 表示入参非法或写入失败。
 */
uint8_t AppParam_Service_SetBaudCode(uint8_t baud_code);

/**
 * @brief  把赛题波特率编码转换为 USART 实际波特率。
 * @param  baud_code 赛题定义的波特率编码。
 * @return 实际波特率。非法编码返回默认 `19200`，保证评分起始链路可恢复。
 */
uint32_t AppParam_Service_BaudCodeToValue(uint8_t baud_code);

/**
 * @brief  判断波特率编码是否合法。
 * @param  baud_code 赛题定义的波特率编码。
 * @return 1 表示合法；0 表示非法。
 */
uint8_t AppParam_Service_IsValidBaudCode(uint8_t baud_code);

/**
 * @brief  获取指定通道当前变比。
 * @param  channel 赛题业务通道，CH0/CH1 有效；CH2 当前不使用变比。
 * @return 当前变比。非法通道返回 1.0f，避免数据面被错误参数放大为异常值。
 */
float AppParam_Service_GetRatio(app_param_channel_t channel);

/**
 * @brief  设置并持久化指定通道变比。
 * @param  channel 赛题业务通道，当前支持 CH0/CH1。
 * @param  ratio IEEE754 单精度变比，必须为有限正数。
 * @return 1 表示写入成功；0 表示通道或数值非法。
 */
uint8_t AppParam_Service_SetRatio(app_param_channel_t channel, float ratio);

/**
 * @brief  获取当前持久化的 DAC 原始输出码。
 * @return 12 位 DAC 原始码，范围 `0..4095`。
 */
uint16_t AppParam_Service_GetDacRaw(void);

/**
 * @brief  设置并持久化 DAC 原始输出码。
 * @param  dac_raw 赛题 DAC 输出原始码，必须位于 `0..4095`。
 * @return 1 表示写入成功；0 表示数值非法或写入失败。
 *
 * DAC 输出是 CH1 回读链路的物理激励源。F 系列会在重启后复查 CH1 变比，
 * 因此必须恢复重启前的 DAC 输出，否则 CH1 会回到 0V 附近，评分器无法
 * 判断 CH1 变比持久化是否仍然生效。
 */
uint8_t AppParam_Service_SetDacRaw(uint16_t dac_raw);

/**
 * @brief  获取指定通道告警阈值。
 * @param  channel 赛题业务通道，支持 CH0/CH1/CH2。
 * @return 当前阈值，单位与对应通道上报值一致。
 */
float AppParam_Service_GetThreshold(app_param_channel_t channel);

/**
 * @brief  设置并持久化指定通道告警阈值。
 * @param  channel 赛题业务通道，支持 CH0/CH1/CH2。
 * @param  threshold IEEE754 单精度阈值，必须为有限非负数。
 * @return 1 表示写入成功；0 表示通道或数值非法。
 */
uint8_t AppParam_Service_SetThreshold(app_param_channel_t channel, float threshold);

/**
 * @brief  获取自动上报间隔编码。
 * @return `0x01=1s`、`0x02=3s`、`0x03=5s`。
 */
uint8_t AppParam_Service_GetReportIntervalCode(void);

/**
 * @brief  设置并持久化自动上报间隔编码。
 * @param  interval_code `0x01=1s`、`0x02=3s`、`0x03=5s`。
 * @return 1 表示写入成功；0 表示编码非法。
 */
uint8_t AppParam_Service_SetReportIntervalCode(uint8_t interval_code);

/**
 * @brief  判断自动上报间隔编码是否合法。
 * @param  interval_code 待检查编码。
 * @return 1 表示合法；0 表示非法。
 */
uint8_t AppParam_Service_IsValidReportIntervalCode(uint8_t interval_code);

/**
 * @brief  获取告警模式编码。
 * @return `0x01=主动上报并存储`、`0x02=仅存储`。
 */
uint8_t AppParam_Service_GetAlarmMode(void);

/**
 * @brief  设置并持久化告警模式编码。
 * @param  alarm_mode `0x01=主动上报并存储`、`0x02=仅存储`。
 * @return 1 表示写入成功；0 表示编码非法。
 */
uint8_t AppParam_Service_SetAlarmMode(uint8_t alarm_mode);

/**
 * @brief  判断告警模式编码是否合法。
 * @param  alarm_mode 待检查编码。
 * @return 1 表示合法；0 表示非法。
 */
uint8_t AppParam_Service_IsValidAlarmMode(uint8_t alarm_mode);

#endif
