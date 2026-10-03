#ifndef __RS485_SERVICE_H
#define __RS485_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief  初始化 USART1 RS485 评分通信服务。
 * @return void 无返回值。
 *
 * 当前 T02 阶段使用赛题默认串口参数 `19200 / 8 / 1 / None`。后续 T03 接入波特率
 * 持久化后，本函数应改为从参数服务读取当前波特率。
 */
void Rs485_Service_Init(void);

/**
 * @brief  RS485 周期任务，由主循环时间片调度器调用。
 * @return void 无返回值。
 *
 * 本任务负责将 USART1 收到的 ASCII-Hex 字符串解码为赛题二进制帧，完成帧结构、
 * CRC、协议版本和设备 ID 过滤后，再分发到业务命令处理逻辑。
 */
void Rs485_Service_Task(void);

/**
 * @brief  发送一帧赛题响应内容帧。
 * @param  command 响应帧携带的命令字。
 * @param  content 响应载荷首地址；content_len 为 0 时允许为 NULL。
 * @param  content_len 响应载荷长度，单位字节。
 * @return void 无返回值；编码失败时静默丢弃，避免输出非协议文本。
 */
void Rs485_Service_SendContent(uint16_t command, const uint8_t *content, uint8_t content_len);

/**
 * @brief  通过 RS485 直接发送原始 ASCII 文本，不做赛题帧封装。
 * @param  text 以 '\0' 结尾的 ASCII 字符串。
 * @return void 无返回值；空指针或空字符串直接忽略。
 *
 * 告警主动上报和告警历史查询按赛题要求必须直发字符串，因此不能复用
 * `Rs485_Service_SendContent()` 的协议封装路径。
 */
void Rs485_Service_SendRawText(const char *text);

/**
 * @brief  读取当前 RTC 时间并转换为赛题帧使用的 Unix 时间戳。
 * @return 1970-01-01 00:00:00 起算的秒数；RTC 数据异常时返回 0。
 */
uint32_t Rs485_Service_GetCurrentTimestamp(void);

/**
 * @brief  把 Unix 时间戳格式化为告警文本使用的本地时间字符串。
 * @param  timestamp Unix 秒级时间戳。
 * @param  out 输出缓冲区。
 * @param  out_len 输出缓冲区长度。
 * @return void 无返回值；缓冲区非法时静默返回。
 *
 * 输出格式固定为 `YYYY-MM-DD HH:MM:SS`，由告警主动上报和历史查询共用。
 */
void Rs485_Service_FormatTimestamp(uint32_t timestamp, char *out, uint16_t out_len);

/**
 * @brief  获取指定业务通道的当前上报值。
 * @param  channel 赛题业务通道，当前 T07 自动上报只使用 CH0/CH1。
 * @return 已经套用业务换算规则后的 float 值。
 */
float Rs485_Service_GetChannelValue(uint8_t channel);

#endif
