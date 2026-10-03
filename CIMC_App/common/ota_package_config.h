#ifndef OTA_PACKAGE_CONFIG_H
#define OTA_PACKAGE_CONFIG_H

/*
 * OTA package header configuration.
 *
 * App_ota.bin layout:
 *   offset 0x00: magic, little-endian
 *   offset 0x04: header_size
 *   offset 0x06: header_version
 *   offset 0x08: app_version
 *   offset 0x0C: app_size, filled by pack tool
 *   offset 0x10: app_crc32, filled by pack tool
 *   offset 0x14: target_addr
 *   offset 0x18: image_type
 *   offset 0x1C: header_crc32, filled by pack tool
 *   offset 0x20: raw App.bin vector table
 */
#define OTA_PACKAGE_MAGIC           0x12345678UL
#define OTA_PACKAGE_HEADER_VERSION  3U
#define OTA_PACKAGE_APP_VERSION     0x00010000UL
#define OTA_PACKAGE_IMAGE_TYPE_APP1 1U
#define OTA_PACKAGE_TARGET_ADDR     0x08011000UL
#define OTA_PACKAGE_MAX_SIZE        0x00020000UL

#endif /* OTA_PACKAGE_CONFIG_H */
