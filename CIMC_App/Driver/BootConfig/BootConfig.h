#ifndef __BOOTCONFIG_H__
#define __BOOTCONFIG_H__

#include "HeaderFiles.h"

#define BOOT_PARAM_AREA_ADDR          0x08010000UL
#define BOOT_PARAM_AREA_SIZE          0x00001000UL

#define BOOT_MAGIC_WORD               0x5AA5C33CUL
#define BOOT_TAIL_MAGIC               0xA5A5C3C3UL
#define BOOT_CALIB_MAGIC              0xCAC0FFEEUL

#define BOOT_APP_ADDR                 0x08011000UL
#define BOOT_APP_SIZE                 0x00020000UL
#define BOOT_APP_BACKUP_ADDR          0x08031000UL
#define BOOT_APP_BACKUP_SIZE          0x00020000UL
#define BOOT_DOWNLOAD_ADDR            0x08051000UL
#define BOOT_DOWNLOAD_SIZE            0x00020000UL

#define BOOT_UPDATE_FLAG_REQUEST      0x5AU
#define BOOT_UPDATE_FLAG_NONE         0x00U
#define BOOT_UPDATE_STATUS_READY      0x01U
#define BOOT_UPDATE_STATUS_DONE       0x00U

typedef struct __attribute__((packed))
{
    uint32_t magicWord;
    uint16_t version;
    uint16_t structSize;
    uint32_t buildDate;
    uint32_t reserved0;

    uint8_t updateFlag;
    uint8_t updateMode;
    uint8_t updateStatus;
    uint8_t updateProgress;
    uint32_t updateCount;
    uint32_t lastUpdateTime;
    uint32_t reserved1;

    uint32_t appSize;
    uint32_t appCRC32;
    uint32_t appVersion;
    uint32_t appBuildDate;
    uint32_t appStartAddr;
    uint32_t appEntryAddr;
    uint32_t appStackAddr;
    uint32_t reserved2;

    uint32_t bootVersion;
    uint32_t bootCRC32;
    uint32_t bootSize;
    uint32_t reserved3;

    uint32_t runTimestamp;
    uint32_t resetCount;
    uint16_t lastResetReason;
    uint16_t bootFailCount;
    uint32_t totalRuntime;
    uint32_t wdtResetCount;
    uint32_t hardFaultCount;
    uint32_t lastErrorCode;
    uint32_t reserved4;

    uint32_t backupFlag;
    uint32_t backupAddr;
    uint32_t backupSize;
    uint32_t backupCRC32;
    uint32_t backupVersion;
    uint32_t backupDate;
    uint32_t reserved5[2];

    uint32_t securityFlag;
    uint32_t encryptKey;
    uint32_t authCode;
    uint32_t reserved6;

    uint8_t deviceID[16];
    uint8_t productModel[16];
    uint8_t serialNumber[16];

    uint32_t hwVersion;
    uint32_t cpuID;
    uint16_t flashSize;
    uint16_t ramSize;
    uint32_t clockFreq;
    uint32_t reserved7[4];

    uint32_t reserved8[2];
    uint32_t paramCRC32;
    uint32_t tailMagic;
} BootParam_t;

typedef struct __attribute__((packed))
{
    uint32_t timestamp;
    uint32_t oldVersion;
    uint32_t newVersion;
    uint32_t newSize;
    uint32_t newCRC32;
    uint8_t status;
    uint8_t mode;
    uint16_t duration;
    uint32_t errorCode;
    uint8_t expand[996];
} UpdateLog_t;

typedef struct __attribute__((packed))
{
    uint32_t uart_baudrate;
    uint8_t uart_parity;
    uint8_t uart_stopbit;
    uint16_t reserved_uart;
    uint32_t can_baudrate;
    uint32_t eth_ip;
    uint32_t eth_mask;
    uint32_t eth_gateway;
    uint32_t reserved_comm[2];

    uint32_t feature_flags;
    uint32_t debug_level;
    uint32_t watchdog_timeout;
    uint32_t reserved_feat[5];

    uint32_t timer_intervals[16];
    uint8_t gpio_config[128];
    uint8_t user_data[128];
    uint8_t reserved[124];
    uint32_t configCRC32;
} UserConfig_t;

typedef struct __attribute__((packed))
{
    uint32_t calib_magic;
    uint32_t calib_date;
    uint32_t calib_version;
    uint32_t reserved0;
    uint16_t adc_offset[16];
    uint16_t dac_gain[16];
    int16_t temp_curve[32];
    float voltage_k[16];
    float current_k[16];
    uint8_t reserved[236];
    uint32_t calibCRC32;
} CalibData_t;

typedef struct __attribute__((packed))
{
    BootParam_t bootParam;
    BootParam_t bootParamBackup;
    UpdateLog_t updateLog;
    UserConfig_t userConfig;
    CalibData_t calibData;
} BootParamImage_t;

void BootConfig_InitDefaults(BootParamImage_t *image);
uint8_t BootConfig_IsValid(const BootParamImage_t *image);
void BootConfig_PrepareForUpdate(BootParamImage_t *image,
                                 uint32_t app_version,
                                 uint32_t app_size,
                                 uint32_t app_crc32,
                                 uint32_t current_app_crc32);

#endif
