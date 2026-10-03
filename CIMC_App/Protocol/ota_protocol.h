#ifndef OTA_UART_PROTOCOL_H
#define OTA_UART_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "ota_package_config.h"

#if defined(__CC_ARM)
#define OTA_PACKED_STRUCT_BEGIN     __packed struct
#define OTA_PACKED_STRUCT_END
#else
#define OTA_PACKED_STRUCT_BEGIN     struct
#define OTA_PACKED_STRUCT_END       __attribute__((packed))
#endif

#define OTA_FRAME_MAGIC             OTA_PACKAGE_MAGIC
#define OTA_STREAM_HEADER_VERSION   OTA_PACKAGE_HEADER_VERSION
#define OTA_IMAGE_TYPE_APP1         OTA_PACKAGE_IMAGE_TYPE_APP1

/**
 * @brief 串口原始文件 OTA 包头。
 *
 * 完整 OTA 文件格式为：
 * [ota_stream_header_t][raw App.bin].
 *
 * App 侧接收器会先解析本包头，再把后续固件数据写入下载缓存区。本包头本身
 * 不会写入下载缓存区，因此 BootLoader 在缓存区起始地址看到的仍然是正常
 * App 向量表。
 */
typedef OTA_PACKED_STRUCT_BEGIN {
    /**< 魔术字，必须等于 OTA_FRAME_MAGIC */
    uint32_t magic;
    /**< 包头大小，单位字节，必须等于 sizeof(ota_stream_header_t) */
    uint16_t header_size;
    /**< 包头版本，必须等于 OTA_STREAM_HEADER_VERSION */
    uint16_t header_version;
    /**< 本 OTA 包携带的 App 版本号 */
    uint32_t app_version;
    /**< raw App.bin 的大小，单位字节 */
    uint32_t app_size;
    /**< 完整 raw App.bin 固件数据的 CRC32 */
    uint32_t app_crc32;
    /**< 固件链接/运行目标地址，必须匹配 BL_APP1_START_ADDR */
    uint32_t target_addr;
    /**< 镜像类型，必须等于 OTA_IMAGE_TYPE_APP1 */
    uint32_t image_type;
    /**< 本字段之前所有包头字段的 CRC32 */
    uint32_t header_crc32;
} OTA_PACKED_STRUCT_END ota_stream_header_t;

/**
 * @brief Validate the raw OTA stream header before Function writes payload to flash.
 * @param stream OTA stream header copied from the receive ring buffer.
 * @return true when magic, version, target region, size, image type, and header CRC all match the project contract.
 */
bool OtaProtocol_IsStreamHeaderValid(const ota_stream_header_t *stream);

#endif /* OTA_UART_PROTOCOL_H */
