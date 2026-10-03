#ifndef COMMON_BL_PARTITION_H
#define COMMON_BL_PARTITION_H

#include <stdint.h>

/*
 * Internal flash layout used by both BootLoader and APP.
 *
 * APP1 is the executable image region. APP_BAK keeps the previous runnable
 * image for rollback. APP2 is kept as the protocol-compatible name for the
 * OTA download cache.
 */
#define BL_FLASH_BASE_ADDR          0x08000000UL
#define BL_FLASH_TOTAL_SIZE         0x00080000UL
#define BL_FLASH_END_ADDR           (BL_FLASH_BASE_ADDR + BL_FLASH_TOTAL_SIZE - 1UL)
#define BL_FLASH_PAGE_SIZE          0x00001000UL

/* BootLoader image. Keep the linker scatter file inside this range. */
#define BL_BOOT_START_ADDR          0x08000000UL
#define BL_BOOT_SIZE                0x00010000UL
#define BL_BOOT_END_ADDR            (BL_BOOT_START_ADDR + BL_BOOT_SIZE - 1UL)

/* One flash page: main parameter copy, backup copy, and compact log entries. */
#define BL_PARAM_START_ADDR         0x08010000UL
#define BL_PARAM_SIZE               0x00001000UL

/* Active application slot. BootLoader jumps here after validation. */
#define BL_APP1_START_ADDR          0x08011000UL
#define BL_APP1_SIZE                0x00020000UL
#define BL_APP1_END_ADDR            (BL_APP1_START_ADDR + BL_APP1_SIZE - 1UL)

/* Previous application image used for rollback after a failed upgrade. */
#define BL_APP_BACKUP_START_ADDR    0x08031000UL
#define BL_APP_BACKUP_SIZE          0x00020000UL
#define BL_APP_BACKUP_END_ADDR      (BL_APP_BACKUP_START_ADDR + BL_APP_BACKUP_SIZE - 1UL)

/* OTA download cache. APP writes new firmware here, BootLoader copies it to APP1. */
#define BL_DOWNLOAD_START_ADDR      0x08051000UL
#define BL_DOWNLOAD_SIZE            0x00020000UL
#define BL_DOWNLOAD_END_ADDR        (BL_DOWNLOAD_START_ADDR + BL_DOWNLOAD_SIZE - 1UL)

/* Backward-compatible names used by the existing OTA protocol/parameter code. */
#define BL_APP2_START_ADDR          BL_DOWNLOAD_START_ADDR
#define BL_APP2_SIZE                BL_DOWNLOAD_SIZE
#define BL_APP2_END_ADDR            (BL_APP2_START_ADDR + BL_APP2_SIZE - 1UL)

#if (BL_BOOT_END_ADDR + 1UL) != BL_PARAM_START_ADDR
#error "BootLoader and parameter page must be contiguous"
#endif

#if (BL_PARAM_START_ADDR + BL_PARAM_SIZE) != BL_APP1_START_ADDR
#error "Parameter page and APP1 must be contiguous"
#endif

#if (BL_APP1_SIZE > BL_APP_BACKUP_SIZE)
#error "Backup region must be large enough for APP1"
#endif

#if (BL_DOWNLOAD_SIZE > BL_APP1_SIZE)
#error "Download cache must not accept images larger than APP1"
#endif

#if BL_DOWNLOAD_END_ADDR > BL_FLASH_END_ADDR
#error "Download cache exceeds internal flash"
#endif

#endif /* COMMON_BL_PARTITION_H */
