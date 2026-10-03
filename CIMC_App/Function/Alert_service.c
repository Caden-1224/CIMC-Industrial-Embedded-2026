#include "Alert_service.h"
#include "App_param_service.h"
#include "Rs485_service.h"
#include "cimc_protocol.h"
#include "bl_param.h"
#include <stddef.h>
#include <stdio.h>

#define ALERT_RECORD_MAGIC          0x414C5231UL
#define ALERT_RECORD_VERSION        0x00010000UL
#define ALERT_RECORD_COUNT          10U
#define ALERT_TEXT_LINE_MAX         72U
#define ALERT_FLASH_ADDR            (BL_PARAM_PAGE_ADDR + 0x00000600UL)
#define ALERT_FLASH_SIZE            0x00000400UL

typedef struct
{
    uint32_t timestamp;
    uint8_t channel;
    uint8_t reserved0;
    uint16_t reserved1;
    float threshold;
    float actual;
} alert_record_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t head;
    uint32_t count;
    alert_record_t records[ALERT_RECORD_COUNT];
    uint32_t crc32;
} alert_store_t;

static alert_store_t s_alert_store;
static uint8_t s_alert_over_threshold[2] = {0U, 0U};
static uint8_t s_alert_page_cache[BL_PARAM_PAGE_SIZE];

static uint32_t Alert_Service_CalcCrc(const alert_store_t *store)
{
    if (store == NULL)
    {
        return 0UL;
    }

    return CimcProtocol_Crc32Calc((const uint8_t *)store,
                                  (uint32_t)offsetof(alert_store_t, crc32));
}

static void Alert_Service_SetDefaults(void)
{
    memset(&s_alert_store, 0, sizeof(s_alert_store));
    s_alert_store.magic = ALERT_RECORD_MAGIC;
    s_alert_store.version = ALERT_RECORD_VERSION;
    s_alert_store.crc32 = Alert_Service_CalcCrc(&s_alert_store);
}

static uint8_t Alert_Service_IsValidStore(const alert_store_t *store)
{
    uint32_t crc;

    if (store == NULL)
    {
        return 0U;
    }
    if ((store->magic != ALERT_RECORD_MAGIC) ||
        (store->version != ALERT_RECORD_VERSION))
    {
        return 0U;
    }
    if ((store->head >= ALERT_RECORD_COUNT) ||
        (store->count > ALERT_RECORD_COUNT))
    {
        return 0U;
    }

    crc = Alert_Service_CalcCrc(store);
    return (crc == store->crc32) ? 1U : 0U;
}

static uint8_t Alert_Service_Commit(void)
{
    /*
     * 告警记录与 APP 参数共用 0x08010000 参数页。提交前先缓存整页，
     * 只替换 0x0600..0x09FF 的告警区域，避免擦写参数页时破坏 Bootloader
     * 主/备参数块、升级元数据或前面已验证的 APP 业务参数。
     */
    if ((ALERT_FLASH_ADDR < BL_PARAM_PAGE_ADDR) ||
        ((ALERT_FLASH_ADDR + ALERT_FLASH_SIZE) > (BL_PARAM_PAGE_ADDR + BL_PARAM_PAGE_SIZE)) ||
        (sizeof(s_alert_store) > ALERT_FLASH_SIZE))
    {
        return 0U;
    }

    s_alert_store.crc32 = Alert_Service_CalcCrc(&s_alert_store);
    memcpy(s_alert_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);
    memset(&s_alert_page_cache[ALERT_FLASH_ADDR - BL_PARAM_PAGE_ADDR], 0xFF, ALERT_FLASH_SIZE);
    memcpy(&s_alert_page_cache[ALERT_FLASH_ADDR - BL_PARAM_PAGE_ADDR],
           &s_alert_store,
           sizeof(s_alert_store));

    InternalFlash_ErasePage(BL_PARAM_PAGE_ADDR);
    InternalFlash_WriteBytes(BL_PARAM_PAGE_ADDR, s_alert_page_cache, BL_PARAM_PAGE_SIZE);
    return 1U;
}

static void Alert_Service_FormatRecord(const alert_record_t *record, char *buffer, uint16_t buffer_len)
{
    char time_text[24];

    if ((record == NULL) || (buffer == NULL) || (buffer_len == 0U))
    {
        return;
    }

    Rs485_Service_FormatTimestamp(record->timestamp, time_text, (uint16_t)sizeof(time_text));
    (void)snprintf(buffer,
                   buffer_len,
                   "%s | CH%u | %.2f | %.2f\r\n",
                   time_text,
                   (unsigned int)record->channel,
                   record->threshold,
                   record->actual);
}

static void Alert_Service_SaveRecord(uint8_t channel, float threshold, float actual)
{
    alert_record_t *record;

    record = &s_alert_store.records[s_alert_store.head];
    record->timestamp = Rs485_Service_GetCurrentTimestamp();
    record->channel = channel;
    record->reserved0 = 0U;
    record->reserved1 = 0U;
    record->threshold = threshold;
    record->actual = actual;

    s_alert_store.sequence++;
    s_alert_store.head = (s_alert_store.head + 1UL) % ALERT_RECORD_COUNT;
    if (s_alert_store.count < ALERT_RECORD_COUNT)
    {
        s_alert_store.count++;
    }

    (void)Alert_Service_Commit();

    if (AppParam_Service_GetAlarmMode() == 0x01U)
    {
        char line[ALERT_TEXT_LINE_MAX];

        Alert_Service_FormatRecord(record, line, (uint16_t)sizeof(line));
        Rs485_Service_SendRawText(line);
    }
}

static void Alert_Service_CheckChannel(uint8_t channel_index)
{
    float threshold;
    float actual;
    uint8_t is_over;

    threshold = AppParam_Service_GetThreshold((app_param_channel_t)channel_index);
    actual = Rs485_Service_GetChannelValue(channel_index);
    is_over = (actual > threshold) ? 1U : 0U;

    if ((is_over != 0U) && (s_alert_over_threshold[channel_index] == 0U))
    {
        Alert_Service_SaveRecord(channel_index, threshold, actual);
    }

    s_alert_over_threshold[channel_index] = is_over;
}

static void Alert_Service_CaptureCurrentEdgeBaseline(void)
{
    float threshold;
    float actual;
    uint8_t channel_index;

    /*
     * 告警记录采用“越阈值上升沿”语义。初始化或清空记录时，当前已经处于
     * 超阈状态的通道只作为基线，不立即写新记录；只有先回落到阈值以下，
     * 再次超过阈值时才生成新告警。这样可以稳定验证 0x0603 后查询 empty。
     */
    for (channel_index = APP_PARAM_CHANNEL_CH0;
         channel_index <= APP_PARAM_CHANNEL_CH1;
         channel_index++)
    {
        threshold = AppParam_Service_GetThreshold((app_param_channel_t)channel_index);
        actual = Rs485_Service_GetChannelValue(channel_index);
        s_alert_over_threshold[channel_index] = (actual > threshold) ? 1U : 0U;
    }
}

void Alert_Service_Init(void)
{
    memcpy(&s_alert_store, (const void *)ALERT_FLASH_ADDR, sizeof(s_alert_store));
    if (!Alert_Service_IsValidStore(&s_alert_store))
    {
        Alert_Service_SetDefaults();
        (void)Alert_Service_Commit();
    }

    Alert_Service_CaptureCurrentEdgeBaseline();
}

void Alert_Service_Task(void)
{
    Alert_Service_CheckChannel(APP_PARAM_CHANNEL_CH0);
    Alert_Service_CheckChannel(APP_PARAM_CHANNEL_CH1);
}

void Alert_Service_SendHistoryText(void)
{
    uint32_t offset;
    uint32_t index;
    char line[ALERT_TEXT_LINE_MAX];

    if (s_alert_store.count == 0UL)
    {
        Rs485_Service_SendRawText("empty");
        return;
    }

    for (offset = 0UL; offset < s_alert_store.count; offset++)
    {
        index = (s_alert_store.head + ALERT_RECORD_COUNT - 1UL - offset) % ALERT_RECORD_COUNT;
        Alert_Service_FormatRecord(&s_alert_store.records[index], line, (uint16_t)sizeof(line));
        Rs485_Service_SendRawText(line);
    }
}

uint8_t Alert_Service_Clear(void)
{
    uint8_t committed;

    Alert_Service_SetDefaults();
    committed = Alert_Service_Commit();
    Alert_Service_CaptureCurrentEdgeBaseline();
    return committed;
}
