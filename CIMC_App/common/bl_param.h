#ifndef COMMON_BL_PARAM_H
#define COMMON_BL_PARAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bl_partition.h"

#define BL_PARAM_PAGE_ADDR          BL_PARAM_START_ADDR
#define BL_PARAM_PAGE_SIZE          BL_PARAM_SIZE
#define BL_PARAM_MAIN_ADDR          BL_PARAM_PAGE_ADDR
#define BL_PARAM_BACKUP_ADDR        (BL_PARAM_PAGE_ADDR + 0x00000100UL)
#define BL_LOG_ADDR                 (BL_PARAM_PAGE_ADDR + 0x00000200UL)
#define BL_LOG_ENTRY_SIZE           32UL
#define BL_LOG_ENTRY_COUNT          32UL

#define BL_PARAM_MAGIC              0x5AA5C33CUL
#define BL_PARAM_TAIL_MAGIC         0xA5A5C3C3UL
#define BL_PARAM_VERSION            0x00010001UL

#define BL_UPDATE_FLAG_IDLE         0x00000000UL
#define BL_UPDATE_FLAG_PENDING      0xAA55AA55UL
#define BL_UPDATE_FLAG_FAILED       0xDEAD0001UL
#define BL_UPDATE_FLAG_FAST_UPGRADE 0x55AA55AAUL
#define BL_UPDATE_FLAG_DOWNLOADED   0xA5A55A5AUL

#define BL_ERR_NONE                 0UL
#define BL_ERR_PARAM_INVALID        1UL
#define BL_ERR_APP2_INVALID         2UL
#define BL_ERR_COPY_FAILED          3UL
#define BL_ERR_APP1_INVALID         4UL

#define BL_LOG_MAGIC                0xB100B100UL
#define BL_LOG_EVENT_PARAM_RECOVER  1UL
#define BL_LOG_EVENT_UPDATE_OK      2UL
#define BL_LOG_EVENT_UPDATE_FAIL    3UL
#define BL_LOG_EVENT_JUMP_FAIL      4UL

#define BL_COPY_CHUNK_SIZE          256UL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t update_flag;
    uint32_t app_size;
    uint32_t app_crc32;
    uint32_t app1_addr;
    uint32_t app2_addr;
    uint32_t update_counter;
    uint32_t fail_counter;
    uint32_t last_error;
    uint32_t log_write_index;
    uint32_t reserved[51];
    uint32_t param_crc32;
    uint32_t tail_magic;
} bl_param_t;

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t event_id;
    uint32_t result;
    uint32_t value0;
    uint32_t value1;
    uint32_t value2;
    uint32_t crc32;
} bl_log_entry_t;

bool bl_commit_param(bl_param_t *param);

#endif /* COMMON_BL_PARAM_H */
