#include "App_param_service.h"
#include "bl_param.h"
#include "cimc_protocol.h"
#include <stddef.h>

#define APP_PARAM_MAGIC             0x43494D43UL
#define APP_PARAM_VERSION           0x00020000UL
#define APP_PARAM_RESERVED_WORDS    51U
#define APP_PARAM_WORD_OFFSET       32U
#define APP_PARAM_WORD_COUNT        16U
#define APP_PARAM_DEFAULT_FLOAT     21.59f

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint16_t device_id;
    uint8_t baud_code;
    uint8_t report_interval_code;
    uint8_t alarm_mode;
    uint8_t reserved0;
    float ch0_ratio;
    float ch1_ratio;
    float ch0_threshold;
    float ch1_threshold;
    float ch2_threshold;
    uint32_t alert_head;
    uint32_t alert_count;
    uint32_t alert_sequence;
    uint16_t dac_raw;
    uint16_t reserved1;
    uint32_t crc32;
} app_param_block_t;

static app_param_block_t s_app_param;
static uint8_t s_param_page_cache[BL_PARAM_PAGE_SIZE];

static uint32_t AppParam_Service_CalcCrc(const app_param_block_t *param)
{
    return CimcProtocol_Crc32Calc((const uint8_t *)param, (uint32_t)offsetof(app_param_block_t, crc32));
}

static uint8_t AppParam_Service_IsValidDeviceId(uint16_t device_id)
{
    return ((device_id >= CIMC_DEVICE_ID_MIN) && (device_id <= CIMC_DEVICE_ID_MAX)) ? 1U : 0U;
}

static uint8_t AppParam_Service_IsFiniteFloat(float value)
{
    return ((value == value) && (value < 3.402823e38f) && (value > -3.402823e38f)) ? 1U : 0U;
}

static uint8_t AppParam_Service_IsValidRatio(float ratio)
{
    return (AppParam_Service_IsFiniteFloat(ratio) && (ratio > 0.0f) && (ratio <= 1000000.0f)) ? 1U : 0U;
}

static uint8_t AppParam_Service_IsValidThreshold(float threshold)
{
    return (AppParam_Service_IsFiniteFloat(threshold) && (threshold >= 0.0f) && (threshold <= 1000000.0f)) ? 1U : 0U;
}

static uint8_t AppParam_Service_IsValidDacRaw(uint16_t dac_raw)
{
    return (dac_raw <= 4095U) ? 1U : 0U;
}

static void AppParam_Service_SetDefaults(app_param_block_t *param)
{
    if (param == NULL)
    {
        return;
    }

    memset(param, 0, sizeof(*param));
    param->magic = APP_PARAM_MAGIC;
    param->version = APP_PARAM_VERSION;
    param->device_id = APP_PARAM_DEFAULT_DEVICE_ID;
    param->baud_code = APP_PARAM_DEFAULT_BAUD_CODE;
    param->report_interval_code = APP_PARAM_DEFAULT_REPORT_CODE;
    param->alarm_mode = APP_PARAM_DEFAULT_ALARM_MODE;
    param->ch0_ratio = 1.0f;
    param->ch1_ratio = 1.0f;
    param->ch0_threshold = APP_PARAM_DEFAULT_FLOAT;
    param->ch1_threshold = APP_PARAM_DEFAULT_FLOAT;
    param->ch2_threshold = APP_PARAM_DEFAULT_FLOAT;
    param->dac_raw = 0U;
    param->crc32 = AppParam_Service_CalcCrc(param);
}

static uint8_t AppParam_Service_IsValidBlock(const app_param_block_t *param)
{
    uint32_t crc;

    if (param == NULL)
    {
        return 0U;
    }
    if ((param->magic != APP_PARAM_MAGIC) || (param->version != APP_PARAM_VERSION))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidDeviceId(param->device_id))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidBaudCode(param->baud_code))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidReportIntervalCode(param->report_interval_code))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidAlarmMode(param->alarm_mode))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidRatio(param->ch0_ratio) ||
        !AppParam_Service_IsValidRatio(param->ch1_ratio))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidThreshold(param->ch0_threshold) ||
        !AppParam_Service_IsValidThreshold(param->ch1_threshold) ||
        !AppParam_Service_IsValidThreshold(param->ch2_threshold))
    {
        return 0U;
    }
    if (!AppParam_Service_IsValidDacRaw(param->dac_raw))
    {
        return 0U;
    }

    crc = AppParam_Service_CalcCrc(param);
    return (crc == param->crc32) ? 1U : 0U;
}

static uint8_t AppParam_Service_IsValidBootParam(const bl_param_t *param)
{
    uint32_t crc;

    if (param == NULL)
    {
        return 0U;
    }
    if ((param->magic != BL_PARAM_MAGIC) ||
        (param->version != BL_PARAM_VERSION) ||
        (param->tail_magic != BL_PARAM_TAIL_MAGIC))
    {
        return 0U;
    }
    if ((param->app1_addr != BL_APP1_START_ADDR) ||
        (param->app2_addr != BL_APP2_START_ADDR))
    {
        return 0U;
    }

    crc = CimcProtocol_Crc32Calc((const uint8_t *)param, (uint32_t)offsetof(bl_param_t, param_crc32));
    return (crc == param->param_crc32) ? 1U : 0U;
}

static void AppParam_Service_LoadBootParam(bl_param_t *param)
{
    bl_param_t main_param;
    bl_param_t backup_param;

    if (param == NULL)
    {
        return;
    }

    memcpy(&main_param, (const void *)BL_PARAM_MAIN_ADDR, sizeof(bl_param_t));
    memcpy(&backup_param, (const void *)BL_PARAM_BACKUP_ADDR, sizeof(bl_param_t));

    if (AppParam_Service_IsValidBootParam(&main_param))
    {
        *param = main_param;
        return;
    }
    if (AppParam_Service_IsValidBootParam(&backup_param))
    {
        *param = backup_param;
        return;
    }

    memset(param, 0, sizeof(*param));
    param->magic = BL_PARAM_MAGIC;
    param->version = BL_PARAM_VERSION;
    param->update_flag = BL_UPDATE_FLAG_IDLE;
    param->app1_addr = BL_APP1_START_ADDR;
    param->app2_addr = BL_APP2_START_ADDR;
    param->tail_magic = BL_PARAM_TAIL_MAGIC;
}

static uint8_t AppParam_Service_Commit(void)
{
    bl_param_t param;
    bl_param_t backup_param;

    AppParam_Service_LoadBootParam(&param);

    /*
     * Bootloader 的 bl_param_t.reserved[] 是共享参数页中预留给后续扩展的空间。
     * 本服务从尾部固定偏移写入 APP 业务块，既不覆盖升级状态、APP 地址和 CRC，
     * 也能随着 Bootloader 的主/备参数提交机制一起获得掉电恢复能力。
     */
    if ((APP_PARAM_WORD_OFFSET + APP_PARAM_WORD_COUNT) > APP_PARAM_RESERVED_WORDS)
    {
        return 0U;
    }

    s_app_param.crc32 = AppParam_Service_CalcCrc(&s_app_param);
    memcpy(&param.reserved[APP_PARAM_WORD_OFFSET], &s_app_param, sizeof(s_app_param));
    param.param_crc32 = CimcProtocol_Crc32Calc((const uint8_t *)&param, (uint32_t)offsetof(bl_param_t, param_crc32));
    backup_param = param;

    memcpy(s_param_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);
    memcpy(&s_param_page_cache[BL_PARAM_MAIN_ADDR - BL_PARAM_PAGE_ADDR], &param, sizeof(param));
    memcpy(&s_param_page_cache[BL_PARAM_BACKUP_ADDR - BL_PARAM_PAGE_ADDR], &backup_param, sizeof(backup_param));

    InternalFlash_ErasePage(BL_PARAM_PAGE_ADDR);
    InternalFlash_WriteBytes(BL_PARAM_PAGE_ADDR, s_param_page_cache, BL_PARAM_PAGE_SIZE);

    return 1U;
}

void AppParam_Service_Init(void)
{
    bl_param_t param;

    AppParam_Service_LoadBootParam(&param);
    memcpy(&s_app_param, &param.reserved[APP_PARAM_WORD_OFFSET], sizeof(s_app_param));

    if (!AppParam_Service_IsValidBlock(&s_app_param))
    {
        AppParam_Service_SetDefaults(&s_app_param);
        (void)AppParam_Service_Commit();
    }
}

uint16_t AppParam_Service_GetDeviceId(void)
{
    return s_app_param.device_id;
}

uint8_t AppParam_Service_SetDeviceId(uint16_t device_id)
{
    if (!AppParam_Service_IsValidDeviceId(device_id))
    {
        return 0U;
    }

    s_app_param.device_id = device_id;
    return AppParam_Service_Commit();
}

uint8_t AppParam_Service_GetBaudCode(void)
{
    return s_app_param.baud_code;
}

uint8_t AppParam_Service_SetBaudCode(uint8_t baud_code)
{
    if (!AppParam_Service_IsValidBaudCode(baud_code))
    {
        return 0U;
    }

    s_app_param.baud_code = baud_code;
    return AppParam_Service_Commit();
}

uint32_t AppParam_Service_BaudCodeToValue(uint8_t baud_code)
{
    switch (baud_code)
    {
        case 0x11U:
            return 4800UL;
        case 0x12U:
            return 9600UL;
        case 0x13U:
            return 19200UL;
        case 0x14U:
            return 115200UL;
        default:
            return 19200UL;
    }
}

uint8_t AppParam_Service_IsValidBaudCode(uint8_t baud_code)
{
    return ((baud_code == 0x11U) ||
            (baud_code == 0x12U) ||
            (baud_code == 0x13U) ||
            (baud_code == 0x14U)) ? 1U : 0U;
}

float AppParam_Service_GetRatio(app_param_channel_t channel)
{
    if (channel == APP_PARAM_CHANNEL_CH0)
    {
        return s_app_param.ch0_ratio;
    }
    if (channel == APP_PARAM_CHANNEL_CH1)
    {
        return s_app_param.ch1_ratio;
    }
    return 1.0f;
}

uint8_t AppParam_Service_SetRatio(app_param_channel_t channel, float ratio)
{
    if (!AppParam_Service_IsValidRatio(ratio))
    {
        return 0U;
    }

    if (channel == APP_PARAM_CHANNEL_CH0)
    {
        s_app_param.ch0_ratio = ratio;
    }
    else if (channel == APP_PARAM_CHANNEL_CH1)
    {
        s_app_param.ch1_ratio = ratio;
    }
    else
    {
        return 0U;
    }

    return AppParam_Service_Commit();
}

uint16_t AppParam_Service_GetDacRaw(void)
{
    return s_app_param.dac_raw;
}

uint8_t AppParam_Service_SetDacRaw(uint16_t dac_raw)
{
    if (!AppParam_Service_IsValidDacRaw(dac_raw))
    {
        return 0U;
    }

    s_app_param.dac_raw = dac_raw;
    return AppParam_Service_Commit();
}

float AppParam_Service_GetThreshold(app_param_channel_t channel)
{
    if (channel == APP_PARAM_CHANNEL_CH0)
    {
        return s_app_param.ch0_threshold;
    }
    if (channel == APP_PARAM_CHANNEL_CH1)
    {
        return s_app_param.ch1_threshold;
    }
    if (channel == APP_PARAM_CHANNEL_CH2)
    {
        return s_app_param.ch2_threshold;
    }
    return 0.0f;
}

uint8_t AppParam_Service_SetThreshold(app_param_channel_t channel, float threshold)
{
    if (!AppParam_Service_IsValidThreshold(threshold))
    {
        return 0U;
    }

    if (channel == APP_PARAM_CHANNEL_CH0)
    {
        s_app_param.ch0_threshold = threshold;
    }
    else if (channel == APP_PARAM_CHANNEL_CH1)
    {
        s_app_param.ch1_threshold = threshold;
    }
    else if (channel == APP_PARAM_CHANNEL_CH2)
    {
        s_app_param.ch2_threshold = threshold;
    }
    else
    {
        return 0U;
    }

    return AppParam_Service_Commit();
}

uint8_t AppParam_Service_GetReportIntervalCode(void)
{
    return s_app_param.report_interval_code;
}

uint8_t AppParam_Service_SetReportIntervalCode(uint8_t interval_code)
{
    if (!AppParam_Service_IsValidReportIntervalCode(interval_code))
    {
        return 0U;
    }

    s_app_param.report_interval_code = interval_code;
    return AppParam_Service_Commit();
}

uint8_t AppParam_Service_IsValidReportIntervalCode(uint8_t interval_code)
{
    return ((interval_code == 0x01U) ||
            (interval_code == 0x02U) ||
            (interval_code == 0x03U)) ? 1U : 0U;
}

uint8_t AppParam_Service_GetAlarmMode(void)
{
    return s_app_param.alarm_mode;
}

uint8_t AppParam_Service_SetAlarmMode(uint8_t alarm_mode)
{
    if (!AppParam_Service_IsValidAlarmMode(alarm_mode))
    {
        return 0U;
    }

    s_app_param.alarm_mode = alarm_mode;
    return AppParam_Service_Commit();
}

uint8_t AppParam_Service_IsValidAlarmMode(uint8_t alarm_mode)
{
    return ((alarm_mode == 0x01U) || (alarm_mode == 0x02U)) ? 1U : 0U;
}
