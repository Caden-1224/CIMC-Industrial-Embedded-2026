#include "AutoReport_service.h"
#include "Rs485_service.h"
#include "App_param_service.h"
#include "Led_service.h"
#include "cimc_protocol.h"

#define AUTO_REPORT_COMMAND              0x0302U
#define AUTO_REPORT_CONTENT_LEN          12U
#define AUTO_REPORT_SAMPLE_LED_INDEX     0U

static uint8_t s_auto_report_running = 0U;
static uint32_t s_auto_report_next_ms = 0UL;

static uint32_t AutoReport_Service_IntervalMs(void)
{
    switch (AppParam_Service_GetReportIntervalCode())
    {
        case 0x01U:
            return 1000UL;
        case 0x02U:
            return 3000UL;
        case 0x03U:
            return 5000UL;
        default:
            return 1000UL;
    }
}

static void AutoReport_Service_SetSampleLed(uint8_t enabled)
{
    if (AUTO_REPORT_SAMPLE_LED_INDEX < LED_NUM)
    {
        led_state[AUTO_REPORT_SAMPLE_LED_INDEX] = (enabled != 0U) ? 1U : 0U;
    }
}

static void AutoReport_Service_SendReportFrame(void)
{
    uint8_t content[AUTO_REPORT_CONTENT_LEN];

    /*
     * 赛题自动上报帧只包含 CH0/CH1：
     *   [0..3]  当前 RTC 对应的 Unix 时间戳，大端传输；
     *   [4..7]  CH0 已乘当前变比后的 float，大端传输；
     *   [8..11] CH1 已乘当前变比后的 float，大端传输。
     * 这里刻意不读取 CH2，避免 T07 依赖外部 PT100 硬件。
     */
    CimcProtocol_WriteBe32(&content[0], Rs485_Service_GetCurrentTimestamp());
    CimcProtocol_WriteFloatBe(&content[4],
                              Rs485_Service_GetChannelValue(APP_PARAM_CHANNEL_CH0));
    CimcProtocol_WriteFloatBe(&content[8],
                              Rs485_Service_GetChannelValue(APP_PARAM_CHANNEL_CH1));

    Rs485_Service_SendContent(AUTO_REPORT_COMMAND, content, AUTO_REPORT_CONTENT_LEN);
}

void AutoReport_Service_Init(void)
{
    s_auto_report_running = 0U;
    s_auto_report_next_ms = 0UL;
    AutoReport_Service_SetSampleLed(0U);
}

void AutoReport_Service_Start(void)
{
    uint32_t now_ms;

    s_auto_report_running = 1U;
    AutoReport_Service_SetSampleLed(1U);

    now_ms = get_millis();
    AutoReport_Service_SendReportFrame();
    s_auto_report_next_ms = now_ms + AutoReport_Service_IntervalMs();
}

void AutoReport_Service_Stop(void)
{
    s_auto_report_running = 0U;
    s_auto_report_next_ms = 0UL;
    AutoReport_Service_SetSampleLed(0U);
}

void AutoReport_Service_Task(void)
{
    uint32_t now_ms;

    if (s_auto_report_running == 0U)
    {
        return;
    }

    now_ms = get_millis();
    if ((uint32_t)(now_ms - s_auto_report_next_ms) < 0x80000000UL)
    {
        AutoReport_Service_SendReportFrame();
        s_auto_report_next_ms = now_ms + AutoReport_Service_IntervalMs();
    }
}

uint8_t AutoReport_Service_IsRunning(void)
{
    return s_auto_report_running;
}
