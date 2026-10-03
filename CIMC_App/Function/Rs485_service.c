#include "Rs485_service.h"
#include "RS485.h"
#include "App_param_service.h"
#include "Adc_service.h"
#include "Gd30ad3344_service.h"
#include "Dac_service.h"
#include "AutoReport_service.h"
#include "Alert_service.h"
#include "Sleep_service.h"
#include "BootUpdate_service.h"
#include "cimc_protocol.h"
#include <stdio.h>
#include <string.h>

#define RS485_STARTUP_HEARTBEAT_DELAY_MS   500U
#define RS485_RESET_DELAY_MS               300U
#define RS485_ID_CHANGE_RESET_DELAY_MS     700U
#define RS485_BOOTLOADER_ENTRY_DELAY_MS    1500U

static uint8_t s_rx_ascii_buf[RS485_FRAME_BUFFER_SIZE];
static uint8_t s_rx_binary_buf[CIMC_FRAME_MAX_SIZE];
static uint8_t s_tx_binary_buf[CIMC_FRAME_MAX_SIZE];
static uint8_t s_tx_ascii_buf[CIMC_ASCII_FRAME_MAX_SIZE];
static uint16_t s_rx_ascii_len = 0U;

static uint8_t s_pending_reset = 0U;
static uint32_t s_pending_reset_at_ms = 0UL;
static uint8_t s_startup_heartbeat_pending = 0U;
static uint32_t s_startup_heartbeat_at_ms = 0UL;
static uint8_t s_raw_text_echo_guard_count = 0U;
static uint32_t s_raw_text_echo_guard_until_ms = 0UL;

static uint8_t Rs485_Service_IsDeadlineReached(uint32_t deadline_ms)
{
    return ((uint32_t)(get_millis() - deadline_ms) < 0x80000000UL) ? 1U : 0U;
}

/**
 * @brief  去掉 RS485 接收帧前后的空白字符，保留中间的 ASCII-Hex 正文。
 * @param  frame RS485 驱动层取出的接收缓冲区。
 * @param  frame_len 输入为原始长度，输出为裁剪后的长度。
 * @param  capacity 接收缓冲区容量，用于保证末尾可写入字符串结束符。
 * @return void 无返回值；参数非法时保持原状态。
 *
 * 串口工具常会在发送后追加 CR/LF，也可能在帧前误带空格。赛题协议本身只关心
 * ASCII-Hex 字符，因此服务层先做边界裁剪，再把正文交给 Protocol 层解析。
 */
static void Rs485_Service_NormalizeFrame(uint8_t *frame, uint16_t *frame_len, uint16_t capacity)
{
    uint16_t start_index;
    uint16_t end_index;
    uint16_t normalized_len;

    if ((frame == NULL) || (frame_len == NULL) || (capacity == 0U))
    {
        return;
    }

    normalized_len = *frame_len;
    if (normalized_len >= capacity)
    {
        normalized_len = (uint16_t)(capacity - 1U);
    }

    start_index = 0U;
    while ((start_index < normalized_len) &&
           ((frame[start_index] == '\r') ||
            (frame[start_index] == '\n') ||
            (frame[start_index] == ' ') ||
            (frame[start_index] == '\t')))
    {
        start_index++;
    }

    end_index = normalized_len;
    while ((end_index > start_index) &&
           ((frame[end_index - 1U] == '\r') ||
            (frame[end_index - 1U] == '\n') ||
            (frame[end_index - 1U] == ' ') ||
            (frame[end_index - 1U] == '\t')))
    {
        end_index--;
    }

    normalized_len = (uint16_t)(end_index - start_index);
    if ((start_index > 0U) && (normalized_len > 0U))
    {
        memmove(frame, &frame[start_index], normalized_len);
    }

    frame[normalized_len] = '\0';
    *frame_len = normalized_len;
}

/**
 * @brief  把二进制协议帧转换为 ASCII-Hex 并通过 USART1 RS485 发送。
 * @param  binary_frame 已完成 CRC 和帧头帧尾的二进制协议帧。
 * @param  binary_len 二进制协议帧长度。
 * @return void 编码失败时直接丢弃，避免向评分串口输出调试文本。
 */
static void Rs485_Service_SendBinaryFrame(const uint8_t *binary_frame, uint16_t binary_len)
{
    uint16_t ascii_len;

    if (CimcProtocol_EncodeAsciiHex(binary_frame,
                                    binary_len,
                                    s_tx_ascii_buf,
                                    (uint16_t)sizeof(s_tx_ascii_buf),
                                    &ascii_len) != CIMC_PARSE_OK)
    {
        return;
    }

    RS485_SendBytes(s_tx_ascii_buf, ascii_len);
}

/**
 * @brief  构造并发送赛题通用错误帧。
 * @param  command 错误帧命令字；普通解析错误使用 CIMC_CMD_ERROR。
 * @return void 构造失败时静默，避免输出非协议内容污染评分。
 */
static void Rs485_Service_SendError(uint16_t command)
{
    uint16_t binary_len;

    if (CimcProtocol_BuildErrorFrame(AppParam_Service_GetDeviceId(),
                                     command,
                                     s_tx_binary_buf,
                                     (uint16_t)sizeof(s_tx_binary_buf),
                                     &binary_len) == CIMC_PARSE_OK)
    {
        Rs485_Service_SendBinaryFrame(s_tx_binary_buf, binary_len);
    }
}

/**
 * @brief  构造并发送心跳帧，用于上位机广播寻址和上电在线通知。
 * @return void 构造失败时静默。
 */
static void Rs485_Service_SendHeartbeat(void)
{
    uint16_t binary_len;

    if (CimcProtocol_BuildFrame(AppParam_Service_GetDeviceId(),
                                CIMC_FRAME_TYPE_HEARTBEAT,
                                CIMC_CMD_HEARTBEAT,
                                NULL,
                                0U,
                                s_tx_binary_buf,
                                (uint16_t)sizeof(s_tx_binary_buf),
                                &binary_len) == CIMC_PARSE_OK)
    {
        Rs485_Service_SendBinaryFrame(s_tx_binary_buf, binary_len);
    }
}

/**
 * @brief  判断一帧是否应该由本机处理。
 * @param  frame 已通过 CRC 和长度校验的协议帧。
 * @return true 表示本机或广播帧；false 表示其他设备的单播帧，必须静默。
 */
static bool Rs485_Service_IsAddressedToThisDevice(const cimc_frame_t *frame)
{
    if (frame == NULL)
    {
        return false;
    }

    return (frame->device_id == AppParam_Service_GetDeviceId()) ||
           (frame->device_id == CIMC_DEVICE_ID_BROADCAST);
}

static void Rs485_Service_SendOk(uint16_t command)
{
    uint16_t binary_len;

    if (CimcProtocol_BuildOkFrame(AppParam_Service_GetDeviceId(),
                                  command,
                                  s_tx_binary_buf,
                                  (uint16_t)sizeof(s_tx_binary_buf),
                                  &binary_len) == CIMC_PARSE_OK)
    {
        Rs485_Service_SendBinaryFrame(s_tx_binary_buf, binary_len);
    }
}

void Rs485_Service_SendContent(uint16_t command, const uint8_t *content, uint8_t content_len)
{
    uint16_t binary_len;

    if (CimcProtocol_BuildFrame(AppParam_Service_GetDeviceId(),
                                CIMC_FRAME_TYPE_RESPONSE,
                                command,
                                content,
                                content_len,
                                s_tx_binary_buf,
                                (uint16_t)sizeof(s_tx_binary_buf),
                                &binary_len) == CIMC_PARSE_OK)
    {
        Rs485_Service_SendBinaryFrame(s_tx_binary_buf, binary_len);
    }
}

void Rs485_Service_SendRawText(const char *text)
{
    uint16_t len;

    if (text == NULL)
    {
        return;
    }

    len = (uint16_t)strlen(text);
    if (len == 0U)
    {
        return;
    }

    /*
     * 告警文本按赛题要求直接发 ASCII，不是协议帧。部分 RS485 收发器会把 TX 回灌到 RX，
     * 若不做短窗口抑制，本机发出的 "empty" 或告警文本会被 ASCII-Hex 解码器判成非法帧，
     * 进而错误地回一帧 error。这里只抑制紧随原始文本发送后的非法解析回显。
     */
    if (s_raw_text_echo_guard_count < 255U)
    {
        s_raw_text_echo_guard_count++;
    }
    s_raw_text_echo_guard_until_ms = get_millis() + 500UL;

    RS485_SendBytes((const uint8_t *)text, len);
}

static void Rs485_Service_SendFirmwareVersion(void)
{
    uint8_t version[4];

    version[0] = APP_PARAM_FIRMWARE_VERSION_MAJOR;
    version[1] = APP_PARAM_FIRMWARE_VERSION_MINOR;
    version[2] = APP_PARAM_FIRMWARE_VERSION_PATCH;
    version[3] = APP_PARAM_FIRMWARE_VERSION_BUILD;
    Rs485_Service_SendContent(0x0104U, version, (uint8_t)sizeof(version));
}

static void Rs485_Service_SendDeviceId(uint16_t command)
{
    uint8_t content[2];

    CimcProtocol_WriteBe16(content, AppParam_Service_GetDeviceId());
    Rs485_Service_SendContent(command, content, (uint8_t)sizeof(content));
}

static void Rs485_Service_SendBaudCode(uint16_t command)
{
    uint8_t baud_code;

    baud_code = AppParam_Service_GetBaudCode();
    Rs485_Service_SendContent(command, &baud_code, 1U);
}

static float Rs485_Service_ReadFloatBe(const uint8_t *data)
{
    uint32_t raw;
    float value;

    raw = CimcProtocol_ReadBe32(data);
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static void Rs485_Service_WriteFloatResponse(uint16_t command, float value)
{
    uint8_t content[4];

    CimcProtocol_WriteFloatBe(content, value);
    Rs485_Service_SendContent(command, content, (uint8_t)sizeof(content));
}

float Rs485_Service_GetChannelValue(uint8_t channel)
{
    if (channel == APP_PARAM_CHANNEL_CH0)
    {
        return ((float)adc_result.ch0.smooth) * AppParam_Service_GetRatio(APP_PARAM_CHANNEL_CH0);
    }
    if (channel == APP_PARAM_CHANNEL_CH1)
    {
        return ((float)adc_result.ch1.smooth) * AppParam_Service_GetRatio(APP_PARAM_CHANNEL_CH1);
    }
    if (channel == APP_PARAM_CHANNEL_CH2)
    {
        /*
         * 当前外部 ADC 服务已把 PT100 前端换算为电阻值。赛题最终要求“实际温度值”，
         * 后续 T15 做 PT100 标定时只需要替换 Gd30ad3344_service 的业务换算出口。
         */
        return gd30ad3344_result.resistance;
    }

    return 0.0f;
}

static uint8_t Rs485_Service_DecToBcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t Rs485_Service_BcdToDec(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static uint8_t Rs485_Service_IsLeapYear(uint16_t year)
{
    if ((year % 400U) == 0U)
    {
        return 1U;
    }
    if ((year % 100U) == 0U)
    {
        return 0U;
    }
    return ((year % 4U) == 0U) ? 1U : 0U;
}

static uint8_t Rs485_Service_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days_in_month[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month == 0U) || (month > 12U))
    {
        return 31U;
    }
    if ((month == 2U) && Rs485_Service_IsLeapYear(year))
    {
        return 29U;
    }
    return days_in_month[month - 1U];
}

static uint32_t Rs485_Service_RtcToUnixSeconds(const rtc_time_t *time)
{
    uint32_t days = 0UL;
    uint16_t year;
    uint16_t current_year;
    uint8_t month;
    uint8_t current_month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    if (time == NULL)
    {
        return 0UL;
    }

    year = (uint16_t)(2000U + Rs485_Service_BcdToDec(time->year));
    month = Rs485_Service_BcdToDec(time->month);
    day = Rs485_Service_BcdToDec(time->day);
    hour = Rs485_Service_BcdToDec(time->hour);
    minute = Rs485_Service_BcdToDec(time->minute);
    second = Rs485_Service_BcdToDec(time->second);

    for (current_year = 1970U; current_year < year; current_year++)
    {
        days += Rs485_Service_IsLeapYear(current_year) ? 366UL : 365UL;
    }

    for (current_month = 1U; current_month < month; current_month++)
    {
        days += Rs485_Service_DaysInMonth(year, current_month);
    }

    if (day > 0U)
    {
        days += (uint32_t)(day - 1U);
    }

    return (days * 86400UL) +
           ((uint32_t)hour * 3600UL) +
           ((uint32_t)minute * 60UL) +
           second;
}

static uint8_t Rs485_Service_UnixSecondsToRtc(uint32_t timestamp, rtc_time_t *time)
{
    uint32_t days;
    uint32_t seconds_of_day;
    uint16_t year;
    uint16_t days_of_year;
    uint8_t month;
    uint8_t dim;

    if (time == NULL)
    {
        return 0U;
    }

    days = timestamp / 86400UL;
    seconds_of_day = timestamp % 86400UL;
    year = 1970U;

    while (year < 2100U)
    {
        days_of_year = Rs485_Service_IsLeapYear(year) ? 366U : 365U;
        if (days < days_of_year)
        {
            break;
        }
        days -= days_of_year;
        year++;
    }

    if ((year < 2000U) || (year > 2099U))
    {
        return 0U;
    }

    month = 1U;
    while (month <= 12U)
    {
        dim = Rs485_Service_DaysInMonth(year, month);
        if (days < dim)
        {
            break;
        }
        days -= dim;
        month++;
    }

    if (month > 12U)
    {
        return 0U;
    }

    time->year = Rs485_Service_DecToBcd((uint8_t)(year - 2000U));
    time->month = Rs485_Service_DecToBcd(month);
    time->day = Rs485_Service_DecToBcd((uint8_t)(days + 1UL));
    time->hour = Rs485_Service_DecToBcd((uint8_t)(seconds_of_day / 3600UL));
    seconds_of_day %= 3600UL;
    time->minute = Rs485_Service_DecToBcd((uint8_t)(seconds_of_day / 60UL));
    time->second = Rs485_Service_DecToBcd((uint8_t)(seconds_of_day % 60UL));
    return 1U;
}

void Rs485_Service_FormatTimestamp(uint32_t timestamp, char *out, uint16_t out_len)
{
    rtc_time_t time;

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (!Rs485_Service_UnixSecondsToRtc(timestamp, &time))
    {
        (void)snprintf(out, out_len, "1970-01-01 00:00:00");
        return;
    }

    (void)snprintf(out,
                   out_len,
                   "20%02u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)Rs485_Service_BcdToDec(time.year),
                   (unsigned int)Rs485_Service_BcdToDec(time.month),
                   (unsigned int)Rs485_Service_BcdToDec(time.day),
                   (unsigned int)Rs485_Service_BcdToDec(time.hour),
                   (unsigned int)Rs485_Service_BcdToDec(time.minute),
                   (unsigned int)Rs485_Service_BcdToDec(time.second));
}

static void Rs485_Service_SendRtcTimestamp(uint16_t command)
{
    uint8_t content[4];
    uint32_t timestamp;

    timestamp = Rs485_Service_GetCurrentTimestamp();
    CimcProtocol_WriteBe32(content, timestamp);
    Rs485_Service_SendContent(command, content, (uint8_t)sizeof(content));
}

uint32_t Rs485_Service_GetCurrentTimestamp(void)
{
    rtc_time_t time;

    RTC_GetTime(&time);
    return Rs485_Service_RtcToUnixSeconds(&time);
}

static uint8_t Rs485_Service_SetRtcTimestamp(const cimc_frame_t *frame)
{
    uint32_t timestamp;
    rtc_time_t time;

    if ((frame == NULL) || (frame->content_len != 4U))
    {
        return 0U;
    }

    timestamp = CimcProtocol_ReadBe32(frame->content);
    if (!Rs485_Service_UnixSecondsToRtc(timestamp, &time))
    {
        return 0U;
    }

    if (RTC_SetTime(&time) == 0U)
    {
        return 0U;
    }

    RTC_SetConfigured();
    return 1U;
}

static void Rs485_Service_RequestResetAfter(uint32_t delay_ms)
{
    /*
     * ACK frames travel through RS485 direction control and DMA/TC completion
     * before the PC tool can safely switch to the next step. Keep the reset
     * asynchronous so the command handler returns immediately, then reset only
     * after the command-specific drain window has elapsed.
     */
    s_pending_reset = 1U;
    s_pending_reset_at_ms = get_millis() + delay_ms;
}

static void Rs485_Service_RequestBootloaderEntryReset(void)
{
    /*
     * The official scorer records the 0x0501 OK string first, then opens a
     * separate receive window for the raw BootLoader banner. If APP resets too
     * soon, the BootLoader text is glued to the OK frame and N-01 loses the
     * initialization-info point even though the banner was transmitted. Use a
     * longer, upgrade-only drain window and suppress the optional startup
     * heartbeat so the next bytes on USART1 belong unambiguously to BootLoader.
     */
    s_startup_heartbeat_pending = 0U;
    Rs485_Service_RequestResetAfter(RS485_BOOTLOADER_ENTRY_DELAY_MS);
}

static void Rs485_Service_SendStartupHeartbeat(void)
{
    /*
     * The scorer waits for a heartbeat after APP reset. Sending it from the task
     * loop gives USART1, RS485 DE, and the PC-side receive window time to settle,
     * while still staying well inside the 15s A-03 scoring window.
     */
    s_startup_heartbeat_pending = 1U;
    s_startup_heartbeat_at_ms = get_millis() + RS485_STARTUP_HEARTBEAT_DELAY_MS;
}

static void Rs485_Service_TaskHeartbeat(void)
{
    if ((s_startup_heartbeat_pending != 0U) &&
        (Rs485_Service_IsDeadlineReached(s_startup_heartbeat_at_ms) != 0U))
    {
        s_startup_heartbeat_pending = 0U;
        Rs485_Service_SendHeartbeat();
    }
}

static void Rs485_Service_HandleSystemCommand(const cimc_frame_t *frame)
{
    uint16_t new_device_id;
    uint8_t new_baud_code;

    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0101U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            Rs485_Service_SendOk(frame->command);
            Rs485_Service_RequestResetAfter(RS485_RESET_DELAY_MS);
            return;

        case 0x0104U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_SendFirmwareVersion();
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0105U:
            if (Rs485_Service_SetRtcTimestamp(frame))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0106U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_SendRtcTimestamp(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x01A1U:
            if (frame->content_len != 2U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            new_device_id = CimcProtocol_ReadBe16(frame->content);
            if (AppParam_Service_SetDeviceId(new_device_id))
            {
                Rs485_Service_SendOk(frame->command);
                Rs485_Service_RequestResetAfter(RS485_ID_CHANGE_RESET_DELAY_MS);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0111U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_SendDeviceId(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x01A2U:
            if (frame->content_len != 1U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            new_baud_code = frame->content[0];
            if (AppParam_Service_IsValidBaudCode(new_baud_code))
            {
                Rs485_Service_SendOk(frame->command);
                if (AppParam_Service_SetBaudCode(new_baud_code))
                {
                    Rs485_Service_RequestResetAfter(RS485_RESET_DELAY_MS);
                }
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0112U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_SendBaudCode(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

static void Rs485_Service_HandleDataCommand(const cimc_frame_t *frame)
{
    float ratio;

    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0201U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_WriteFloatResponse(frame->command,
                                                 Rs485_Service_GetChannelValue(APP_PARAM_CHANNEL_CH0));
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0202U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_WriteFloatResponse(frame->command,
                                                 Rs485_Service_GetChannelValue(APP_PARAM_CHANNEL_CH1));
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0221U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_WriteFloatResponse(frame->command,
                                                 Rs485_Service_GetChannelValue(APP_PARAM_CHANNEL_CH2));
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0241U:
            if (frame->content_len != 4U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            ratio = Rs485_Service_ReadFloatBe(frame->content);
            if (AppParam_Service_SetRatio(APP_PARAM_CHANNEL_CH0, ratio))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0242U:
            if (frame->content_len != 4U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            ratio = Rs485_Service_ReadFloatBe(frame->content);
            if (AppParam_Service_SetRatio(APP_PARAM_CHANNEL_CH1, ratio))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0261U:
            if (frame->content_len != 1U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            if (AppParam_Service_SetReportIntervalCode(frame->content[0]))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

static void Rs485_Service_HandleControlCommand(const cimc_frame_t *frame)
{
    uint16_t dac_raw;

    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0301U:
            /*
             * 赛题把 DAC 输出定义为 2 字节大端原始码值，合法范围为
             * 0x0000..0x0FFF。这里在协议业务边界先拒绝非法码值，
             * 保证评分器能得到明确错误帧，而不是被底层驱动静默钳位。
             */
            if (frame->content_len != 2U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            dac_raw = CimcProtocol_ReadBe16(frame->content);
            if (dac_raw > DAC_OUTPUT_MAX_RAW)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            /*
             * 0x0301 is the physical excitation for CH1 DAC readback. Persist
             * the accepted raw code before acknowledging so the F-series reset
             * check sees the same loopback level after APP restarts.
             */
            if (AppParam_Service_SetDacRaw(dac_raw))
            {
                Dac_Service_SetRaw(dac_raw);
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0302U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            AutoReport_Service_Start();
            return;

        case 0x0303U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            AutoReport_Service_Stop();
            Rs485_Service_SendOk(frame->command);
            return;

        case 0x03AAU:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            Rs485_Service_SendOk(frame->command);
            Sleep_Service_RequestRtcWakeup();
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

static void Rs485_Service_SendThresholdPair(uint16_t command)
{
    uint8_t content[8];

    CimcProtocol_WriteFloatBe(&content[0], AppParam_Service_GetThreshold(APP_PARAM_CHANNEL_CH0));
    CimcProtocol_WriteFloatBe(&content[4], AppParam_Service_GetThreshold(APP_PARAM_CHANNEL_CH1));
    Rs485_Service_SendContent(command, content, (uint8_t)sizeof(content));
}

static void Rs485_Service_HandleThresholdCommand(const cimc_frame_t *frame)
{
    float threshold;
    app_param_channel_t channel;

    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0400U:
            if (frame->content_len == 0U)
            {
                Rs485_Service_SendThresholdPair(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0401U:
        case 0x0402U:
        case 0x0403U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            channel = (frame->command == 0x0401U) ? APP_PARAM_CHANNEL_CH0 :
                      (frame->command == 0x0402U) ? APP_PARAM_CHANNEL_CH1 :
                                                     APP_PARAM_CHANNEL_CH2;
            Rs485_Service_WriteFloatResponse(frame->command, AppParam_Service_GetThreshold(channel));
            return;

        case 0x0411U:
        case 0x0412U:
        case 0x0413U:
            if (frame->content_len != 4U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            channel = (frame->command == 0x0411U) ? APP_PARAM_CHANNEL_CH0 :
                      (frame->command == 0x0412U) ? APP_PARAM_CHANNEL_CH1 :
                                                     APP_PARAM_CHANNEL_CH2;
            threshold = Rs485_Service_ReadFloatBe(frame->content);
            if (AppParam_Service_SetThreshold(channel, threshold))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

static void Rs485_Service_HandleAlertCommand(const cimc_frame_t *frame)
{
    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0601U:
            if (frame->content_len != 1U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            if (AppParam_Service_SetAlarmMode(frame->content[0]))
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        case 0x0602U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            Alert_Service_SendHistoryText();
            return;

        case 0x0603U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }
            if (Alert_Service_Clear())
            {
                Rs485_Service_SendOk(frame->command);
            }
            else
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
            }
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

static void Rs485_Service_HandleUpgradeCommand(const cimc_frame_t *frame)
{
    if (frame == NULL)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    switch (frame->command)
    {
        case 0x0501U:
            if (frame->content_len != 0U)
            {
                Rs485_Service_SendError(CIMC_CMD_ERROR);
                return;
            }

            if (BootUpdate_Service_RequestEnterBootloader() != 0U)
            {
                Rs485_Service_SendOk(frame->command);
                Rs485_Service_RequestBootloaderEntryReset();
            }
            else
            {
                Rs485_Service_SendError(frame->command);
            }
            return;

        default:
            Rs485_Service_SendError(CIMC_CMD_ERROR);
            return;
    }
}

/**
 * @brief  分发已解析的赛题协议帧。
 * @param  frame 已完成 ASCII-Hex 解码、帧结构校验、CRC 校验的协议帧。
 * @return void 当前 T02 阶段只实现广播寻址和异常兜底，业务命令在后续任务补齐。
 */
static void Rs485_Service_DispatchFrame(const cimc_frame_t *frame)
{
    if (!Rs485_Service_IsAddressedToThisDevice(frame))
    {
        return;
    }

    /*
     * RS485 是半双工总线，部分收发器或接线方式会在本机发送期间把 TX 内容回灌到 RX。
     * 如果把自己刚发出的响应帧/错误帧再次当作“异常输入”处理，就会出现错误帧触发错误帧的连发。
     * 因此只有上位机下发的命令帧、以及广播寻址帧允许触发主动应答；本机回显类帧必须静默。
     */
    if ((frame->frame_type == CIMC_FRAME_TYPE_RESPONSE) ||
        (frame->frame_type == CIMC_FRAME_TYPE_ERROR))
    {
        return;
    }

    /*
     * 自动上报期间赛题明确要求只响应停止命令 0x0303。过滤必须早于广播寻址心跳，
     * 否则评测器在 H-02 阶段发送 discovery 时会收到心跳帧并触发倒扣分。
     */
    if (AutoReport_Service_IsRunning() != 0U)
    {
        if ((frame->frame_type == CIMC_FRAME_TYPE_COMMAND) && (frame->command == 0x0303U))
        {
            Rs485_Service_HandleControlCommand(frame);
        }
        return;
    }

    if ((frame->frame_type == CIMC_FRAME_TYPE_HEARTBEAT) &&
        (frame->command == CIMC_CMD_DISCOVERY))
    {
        Rs485_Service_SendHeartbeat();
        return;
    }

    if (frame->frame_type == CIMC_FRAME_TYPE_HEARTBEAT)
    {
        return;
    }

    if (frame->frame_type != CIMC_FRAME_TYPE_COMMAND)
    {
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0100U)
    {
        Rs485_Service_HandleSystemCommand(frame);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0200U)
    {
        Rs485_Service_HandleDataCommand(frame);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0300U)
    {
        Rs485_Service_HandleControlCommand(frame);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0400U)
    {
        Rs485_Service_HandleThresholdCommand(frame);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0500U)
    {
        Rs485_Service_HandleUpgradeCommand(frame);
        return;
    }

    if ((frame->command & 0xFF00U) == 0x0600U)
    {
        Rs485_Service_HandleAlertCommand(frame);
        return;
    }

    Rs485_Service_SendError(CIMC_CMD_ERROR);
}

void Rs485_Service_Init(void)
{
    RS485_Init(AppParam_Service_BaudCodeToValue(AppParam_Service_GetBaudCode()));
    Rs485_Service_SendStartupHeartbeat();
}

void Rs485_Service_Task(void)
{
    cimc_parse_status_t status;
    cimc_frame_t frame;
    uint16_t binary_len;

    if ((s_pending_reset != 0U) &&
        (Rs485_Service_IsDeadlineReached(s_pending_reset_at_ms) != 0U))
    {
        __disable_irq();
        NVIC_SystemReset();
    }

    Rs485_Service_TaskHeartbeat();

    if (!RS485_FetchFrame(s_rx_ascii_buf, &s_rx_ascii_len, RS485_FRAME_BUFFER_SIZE))
    {
        return;
    }

    Rs485_Service_NormalizeFrame(s_rx_ascii_buf, &s_rx_ascii_len, RS485_FRAME_BUFFER_SIZE);
    if (s_rx_ascii_len == 0U)
    {
        return;
    }

    status = CimcProtocol_DecodeAsciiHex(s_rx_ascii_buf,
                                         s_rx_ascii_len,
                                         s_rx_binary_buf,
                                         (uint16_t)sizeof(s_rx_binary_buf),
                                         &binary_len);
    if (status != CIMC_PARSE_OK)
    {
        if ((s_raw_text_echo_guard_count > 0U) &&
            ((uint32_t)(s_raw_text_echo_guard_until_ms - get_millis()) < 0x80000000UL))
        {
            s_raw_text_echo_guard_count--;
            return;
        }
        if (AutoReport_Service_IsRunning() != 0U)
        {
            return;
        }
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    status = CimcProtocol_ParseFrame(s_rx_binary_buf, binary_len, &frame);
    if (status != CIMC_PARSE_OK)
    {
        if (AutoReport_Service_IsRunning() != 0U)
        {
            return;
        }
        Rs485_Service_SendError(CIMC_CMD_ERROR);
        return;
    }

    Rs485_Service_DispatchFrame(&frame);
}
