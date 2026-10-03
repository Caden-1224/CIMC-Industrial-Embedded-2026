#include "BootUpdate_service.h"
#include "USART.h"
#include "systick.h"
#include "bl_partition.h"
#include "bl_param.h"
#include "cimc_protocol.h"
#include "ota_protocol.h"
#include "ring_buffer.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define OTA_RING_BUFFER_SIZE        (32U * 1024U)
#define OTA_STREAM_WRITE_CHUNK      512U

typedef enum
{
    OTA_STATE_WAIT_HEADER = 0,
    OTA_STATE_RECV_STREAM
} ota_state_t;

typedef struct
{
    ota_state_t state;
    uint32_t app_version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t recv_size;
    uint32_t crc_acc;
    bool rx_overflow;
} ota_context_t;

static ota_context_t s_ota_ctx;
static uint8_t s_param_page_cache[BL_PARAM_PAGE_SIZE];
static uint8_t s_ring_buffer_storage[OTA_RING_BUFFER_SIZE];
static ring_buffer_t s_ring_buffer;

static uint32_t param_calc_crc(const bl_param_t *param)
{
    return CimcProtocol_Crc32Calc((const uint8_t *)param, (uint32_t)offsetof(bl_param_t, param_crc32));
}

static bool flash_wait_ready(void)
{
    uint32_t timeout = 0x3FFFFFUL;

    while ((fmc_flag_get(FMC_FLAG_BUSY) != RESET) && (timeout > 0UL))
    {
        timeout--;
    }

    return (timeout > 0UL);
}

static void flash_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
}

static bool flash_erase_pages(uint32_t start_addr, uint32_t size)
{
    uint32_t end_addr;
    uint32_t page_addr;

    if ((size == 0UL) || (start_addr < BL_FLASH_BASE_ADDR))
    {
        return false;
    }

    end_addr = start_addr + size - 1UL;
    if (end_addr > BL_FLASH_END_ADDR)
    {
        return false;
    }

    page_addr = start_addr - (start_addr % BL_FLASH_PAGE_SIZE);
    end_addr = end_addr - (end_addr % BL_FLASH_PAGE_SIZE);

    fmc_unlock();
    flash_clear_flags();

    while (page_addr <= end_addr)
    {
        fmc_page_erase(page_addr);
        if (!flash_wait_ready())
        {
            fmc_lock();
            return false;
        }
        flash_clear_flags();
        page_addr += BL_FLASH_PAGE_SIZE;
    }

    fmc_lock();
    return true;
}

static bool flash_program_bytes(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t word_value;

    if ((data == NULL) || (size == 0UL))
    {
        return false;
    }

    if ((addr < BL_FLASH_BASE_ADDR) || ((addr + size - 1UL) > BL_FLASH_END_ADDR))
    {
        return false;
    }

    fmc_unlock();
    flash_clear_flags();

    while (((addr & 3UL) == 0UL) && (size >= 4UL))
    {
        memcpy(&word_value, data, sizeof(word_value));
        fmc_word_program(addr, word_value);
        if (!flash_wait_ready())
        {
            fmc_lock();
            return false;
        }
        addr += 4UL;
        data += 4UL;
        size -= 4UL;
    }

    for (i = 0UL; i < size; i++)
    {
        fmc_byte_program(addr + i, data[i]);
        if (!flash_wait_ready())
        {
            fmc_lock();
            return false;
        }
    }

    flash_clear_flags();
    fmc_lock();
    return true;
}

static void param_set_default(bl_param_t *param)
{
    memset(param, 0, sizeof(bl_param_t));
    param->magic = BL_PARAM_MAGIC;
    param->version = BL_PARAM_VERSION;
    param->update_flag = BL_UPDATE_FLAG_IDLE;
    param->app1_addr = BL_APP1_START_ADDR;
    param->app2_addr = BL_APP2_START_ADDR;
    param->tail_magic = BL_PARAM_TAIL_MAGIC;
    param->param_crc32 = param_calc_crc(param);
}

static bool param_is_min_valid(const bl_param_t *param)
{
    return (param->magic == BL_PARAM_MAGIC) &&
           (param->tail_magic == BL_PARAM_TAIL_MAGIC) &&
           (param->version == BL_PARAM_VERSION);
}

static bool param_commit_update(uint32_t app_size, uint32_t app_crc32)
{
    bl_param_t main_param;
    bl_param_t backup_param;

    memcpy(s_param_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);
    memcpy(&main_param, (const void *)BL_PARAM_MAIN_ADDR, sizeof(bl_param_t));
    memcpy(&backup_param, (const void *)BL_PARAM_BACKUP_ADDR, sizeof(bl_param_t));

    if (!param_is_min_valid(&main_param))
    {
        if (param_is_min_valid(&backup_param))
        {
            main_param = backup_param;
        }
        else
        {
            param_set_default(&main_param);
        }
    }

    main_param.app_size = app_size;
    main_param.app_crc32 = app_crc32;
    main_param.app1_addr = BL_APP1_START_ADDR;
    main_param.app2_addr = BL_APP2_START_ADDR;
    main_param.update_flag = BL_UPDATE_FLAG_PENDING;
    main_param.last_error = BL_ERR_NONE;
    main_param.param_crc32 = param_calc_crc(&main_param);
    backup_param = main_param;

    memcpy(&s_param_page_cache[BL_PARAM_MAIN_ADDR - BL_PARAM_PAGE_ADDR], &main_param, sizeof(bl_param_t));
    memcpy(&s_param_page_cache[BL_PARAM_BACKUP_ADDR - BL_PARAM_PAGE_ADDR], &backup_param, sizeof(bl_param_t));

    if (!flash_erase_pages(BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE))
    {
        return false;
    }

    return flash_program_bytes(BL_PARAM_PAGE_ADDR, s_param_page_cache, BL_PARAM_PAGE_SIZE);
}

uint8_t BootUpdate_Service_RequestEnterBootloader(void)
{
    bl_param_t main_param;
    bl_param_t backup_param;

    /*
     * 0x0501 only means "reboot into BootLoader and wait for a package".
     * Keep it separate from BL_UPDATE_FLAG_PENDING, because PENDING means the
     * download cache already contains a complete image that BootLoader should
     * install immediately.
     */
    memcpy(s_param_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);
    memcpy(&main_param, (const void *)BL_PARAM_MAIN_ADDR, sizeof(bl_param_t));
    memcpy(&backup_param, (const void *)BL_PARAM_BACKUP_ADDR, sizeof(bl_param_t));

    if (!param_is_min_valid(&main_param))
    {
        if (param_is_min_valid(&backup_param))
        {
            main_param = backup_param;
        }
        else
        {
            param_set_default(&main_param);
        }
    }

    main_param.update_flag = BL_UPDATE_FLAG_FAST_UPGRADE;
    main_param.last_error = BL_ERR_NONE;
    main_param.app1_addr = BL_APP1_START_ADDR;
    main_param.app2_addr = BL_APP2_START_ADDR;
    main_param.tail_magic = BL_PARAM_TAIL_MAGIC;
    main_param.param_crc32 = param_calc_crc(&main_param);
    backup_param = main_param;

    memcpy(&s_param_page_cache[BL_PARAM_MAIN_ADDR - BL_PARAM_PAGE_ADDR], &main_param, sizeof(bl_param_t));
    memcpy(&s_param_page_cache[BL_PARAM_BACKUP_ADDR - BL_PARAM_PAGE_ADDR], &backup_param, sizeof(bl_param_t));

    if (!flash_erase_pages(BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE))
    {
        return 0U;
    }

    return flash_program_bytes(BL_PARAM_PAGE_ADDR, s_param_page_cache, BL_PARAM_PAGE_SIZE) ? 1U : 0U;
}

static bool ota_begin_stream(const ota_stream_header_t *stream)
{
    uint32_t erase_size;

    erase_size = (stream->app_size + BL_FLASH_PAGE_SIZE - 1UL) & ~(BL_FLASH_PAGE_SIZE - 1UL);
    if (!flash_erase_pages(BL_APP2_START_ADDR, erase_size))
    {
        return false;
    }

    s_ota_ctx.state = OTA_STATE_RECV_STREAM;
    s_ota_ctx.app_version = stream->app_version;
    s_ota_ctx.expected_size = stream->app_size;
    s_ota_ctx.expected_crc32 = stream->app_crc32;
    s_ota_ctx.recv_size = 0UL;
    s_ota_ctx.crc_acc = 0xFFFFFFFFUL;
    s_ota_ctx.rx_overflow = false;
    return true;
}

static bool ota_write_payload(const uint8_t *payload, uint32_t len)
{
    uint32_t flash_addr;

    if ((payload == NULL) || (len == 0UL))
    {
        return true;
    }

    if ((s_ota_ctx.recv_size + len) > s_ota_ctx.expected_size)
    {
        USART0_Printf("OTA: size overflow\r\n");
        return false;
    }

    flash_addr = BL_APP2_START_ADDR + s_ota_ctx.recv_size;
    if (!flash_program_bytes(flash_addr, payload, len))
    {
        USART0_Printf("OTA: flash write failed\r\n");
        return false;
    }

    s_ota_ctx.crc_acc = CimcProtocol_Crc32Update(s_ota_ctx.crc_acc, payload, len);
    s_ota_ctx.recv_size += len;
    return true;
}

static bool ota_finalize_if_done(void)
{
    uint32_t crc_value;

    if (s_ota_ctx.recv_size != s_ota_ctx.expected_size)
    {
        return false;
    }

    if (s_ota_ctx.rx_overflow)
    {
        USART0_Printf("OTA: rx overflow\r\n");
        return false;
    }

    crc_value = CimcProtocol_Crc32Finalize(s_ota_ctx.crc_acc);
    if (crc_value != s_ota_ctx.expected_crc32)
    {
        USART0_Printf("OTA: crc mismatch exp=0x%08lx got=0x%08lx\r\n",
                      s_ota_ctx.expected_crc32, crc_value);
        return false;
    }

    if (!param_commit_update(s_ota_ctx.expected_size, s_ota_ctx.expected_crc32))
    {
        USART0_Printf("OTA: param commit failed\r\n");
        return false;
    }

    return true;
}

static bool ota_stream_consume_data(void)
{
    uint8_t payload[OTA_STREAM_WRITE_CHUNK];
    uint32_t available;
    uint32_t remain;
    uint32_t chunk_size;

    while (s_ota_ctx.state == OTA_STATE_RECV_STREAM)
    {
        available = ring_buffer_available(&s_ring_buffer);
        if (available == 0UL)
        {
            return true;
        }

        remain = s_ota_ctx.expected_size - s_ota_ctx.recv_size;
        chunk_size = (available < remain) ? available : remain;
        if (chunk_size > OTA_STREAM_WRITE_CHUNK)
        {
            chunk_size = OTA_STREAM_WRITE_CHUNK;
        }

        (void)ring_buffer_read(&s_ring_buffer, payload, chunk_size);
        if (!ota_write_payload(payload, chunk_size))
        {
            USART0_Printf("OTA: write failed at %lu\r\n", s_ota_ctx.recv_size);
            return false;
        }

        if (s_ota_ctx.recv_size == s_ota_ctx.expected_size)
        {
            if (!ota_finalize_if_done())
            {
                return false;
            }
            USART0_Printf("OTA: done, reset to bootloader\r\n");
            delay_1ms(100U);
            __disable_irq();
            NVIC_SystemReset();
        }
    }

    return true;
}

void BootUpdate_Service_Init(void)
{
    memset(&s_ota_ctx, 0, sizeof(s_ota_ctx));
    s_ota_ctx.state = OTA_STATE_WAIT_HEADER;
    s_ota_ctx.crc_acc = 0xFFFFFFFFUL;
    ring_buffer_init(&s_ring_buffer, s_ring_buffer_storage, OTA_RING_BUFFER_SIZE);
}

void BootUpdate_Service_Task(void)
{
    ota_stream_header_t stream_header;
    uint32_t available;

    while (1)
    {
        if (s_ota_ctx.state == OTA_STATE_RECV_STREAM)
        {
            if (!ota_stream_consume_data())
            {
                BootUpdate_Service_Init();
            }
            return;
        }

        available = ring_buffer_available(&s_ring_buffer);
        if (available < sizeof(uint32_t))
        {
            return;
        }

        (void)ring_buffer_peek(&s_ring_buffer, (uint8_t *)&stream_header.magic, sizeof(stream_header.magic));
        if (stream_header.magic != OTA_FRAME_MAGIC)
        {
            ring_buffer_drop(&s_ring_buffer, 1UL);
            continue;
        }

        if (available < sizeof(ota_stream_header_t))
        {
            return;
        }

        (void)ring_buffer_peek(&s_ring_buffer, (uint8_t *)&stream_header, sizeof(stream_header));
        if (!OtaProtocol_IsStreamHeaderValid(&stream_header))
        {
            ring_buffer_drop(&s_ring_buffer, 1UL);
            continue;
        }

        ring_buffer_drop(&s_ring_buffer, (uint32_t)sizeof(ota_stream_header_t));
        if (!ota_begin_stream(&stream_header))
        {
            USART0_Printf("OTA: begin failed\r\n");
            BootUpdate_Service_Init();
            return;
        }

        USART0_Printf("OTA: begin version=0x%08lx size=%lu\r\n",
                      stream_header.app_version, stream_header.app_size);
    }
}

uint8_t BootUpdate_Service_HandleFrame(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t handled;

    if ((frame == NULL) || (frame_len == 0U))
    {
        return 0U;
    }

    handled = (s_ota_ctx.state == OTA_STATE_RECV_STREAM) ? 1U : 0U;
    if ((frame_len >= sizeof(uint32_t)) &&
        ((((uint32_t)frame[0]) |
          ((uint32_t)frame[1] << 8) |
          ((uint32_t)frame[2] << 16) |
          ((uint32_t)frame[3] << 24)) == OTA_FRAME_MAGIC))
    {
        handled = 1U;
    }

    if (!ring_buffer_write(&s_ring_buffer, frame, frame_len))
    {
        s_ota_ctx.rx_overflow = true;
        USART0_Printf("OTA: ring buffer overflow\r\n");
        handled = 1U;
    }

    BootUpdate_Service_Task();
    if (s_ota_ctx.state == OTA_STATE_RECV_STREAM)
    {
        handled = 1U;
    }

    return handled;
}
