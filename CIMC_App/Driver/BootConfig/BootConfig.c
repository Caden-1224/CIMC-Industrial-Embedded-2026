#include "BootConfig.h"

void BootConfig_InitDefaults(BootParamImage_t *image)
{
    if (image == NULL)
    {
        return;
    }

    memset(image, 0, sizeof(*image));

    image->bootParam.magicWord = BOOT_MAGIC_WORD;
    image->bootParam.version = 0x0001U;
    image->bootParam.structSize = sizeof(BootParam_t);
    image->bootParam.updateMode = 0x01U;
    image->bootParam.updateFlag = BOOT_UPDATE_FLAG_NONE;
    image->bootParam.updateStatus = BOOT_UPDATE_STATUS_DONE;
    image->bootParam.appStartAddr = BOOT_APP_ADDR;
    image->bootParam.appEntryAddr = *(__IO uint32_t *)(BOOT_APP_ADDR + 4UL);
    image->bootParam.appStackAddr = *(__IO uint32_t *)(BOOT_APP_ADDR + 0UL);
    image->bootParam.bootVersion = 0x00000001UL;
    image->bootParam.bootSize = 0x00010000UL;
    image->bootParam.backupAddr = BOOT_APP_BACKUP_ADDR;
    image->bootParam.backupSize = BOOT_APP_SIZE;
    image->bootParam.hwVersion = 1UL;
    image->bootParam.cpuID = 1UL;
    image->bootParam.flashSize = 512U;
    image->bootParam.ramSize = 192U;
    image->bootParam.clockFreq = 240000000UL;
    image->bootParam.tailMagic = BOOT_TAIL_MAGIC;

    memcpy(image->bootParam.deviceID, "202606020001", 12U);
    memcpy(image->bootParam.productModel, "GD32F470APP", 11U);
    memcpy(image->bootParam.serialNumber, "BOOTAPP00001", 12U);

    image->bootParamBackup = image->bootParam;

    image->updateLog.mode = 1U;
    image->userConfig.uart_baudrate = 19200UL;
    image->userConfig.uart_stopbit = 1U;
    image->calibData.calib_magic = BOOT_CALIB_MAGIC;
}

uint8_t BootConfig_IsValid(const BootParamImage_t *image)
{
    if (image == NULL)
    {
        return 0U;
    }

    if (image->bootParam.magicWord != BOOT_MAGIC_WORD)
    {
        return 0U;
    }

    if (image->bootParam.tailMagic != BOOT_TAIL_MAGIC)
    {
        return 0U;
    }

    if (image->bootParam.structSize != sizeof(BootParam_t))
    {
        return 0U;
    }

    return 1U;
}

void BootConfig_PrepareForUpdate(BootParamImage_t *image,
                                 uint32_t app_version,
                                 uint32_t app_size,
                                 uint32_t app_crc32,
                                 uint32_t current_app_crc32)
{
    if (image == NULL)
    {
        return;
    }

    if (BootConfig_IsValid(image) == 0U)
    {
        BootConfig_InitDefaults(image);
    }

    image->bootParamBackup = image->bootParam;

    image->bootParam.magicWord = BOOT_MAGIC_WORD;
    image->bootParam.updateFlag = BOOT_UPDATE_FLAG_REQUEST;
    image->bootParam.updateMode = 0x01U;
    image->bootParam.updateStatus = BOOT_UPDATE_STATUS_READY;
    image->bootParam.updateProgress = 100U;
    image->bootParam.appSize = app_size;
    image->bootParam.appCRC32 = app_crc32;
    image->bootParam.appVersion = app_version;
    image->bootParam.appStartAddr = BOOT_APP_ADDR;
    image->bootParam.appEntryAddr = *(__IO uint32_t *)(BOOT_DOWNLOAD_ADDR + 4UL);
    image->bootParam.appStackAddr = *(__IO uint32_t *)(BOOT_DOWNLOAD_ADDR + 0UL);
    image->bootParam.backupAddr = BOOT_APP_BACKUP_ADDR;
    image->bootParam.backupSize = BOOT_APP_SIZE;
    image->bootParam.backupCRC32 = current_app_crc32;
    image->bootParam.backupVersion = image->bootParamBackup.appVersion;
    image->bootParam.tailMagic = BOOT_TAIL_MAGIC;
}
