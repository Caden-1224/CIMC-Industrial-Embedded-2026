#ifndef __SPI_FLASH_SERVICE_H
#define __SPI_FLASH_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief 初始化 SPI Flash 服务并缓存一次 JEDEC ID 探测结果。
 * @param void 无输入参数。
 * @return void 无返回值。
 *
 * 该函数初始化底层 SPI Flash 驱动，读取一次器件 ID，
 * 仅当 ID 不是明显的总线故障值时才标记服务就绪。
 */
void SpiFlash_Service_Init(void);

/**
 * @brief 查询 SPI Flash 服务是否可以接受读写请求。
 * @param void 无输入参数。
 * @return uint8_t 服务就绪返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_IsReady(void);

/**
 * @brief 获取初始化时缓存的 JEDEC ID。
 * @param void 无输入参数。
 * @return uint32_t 缓存的 24 位 JEDEC ID。
 */
uint32_t SpiFlash_Service_GetId(void);

/**
 * @brief 从 SPI Flash 读取数据。
 * @param read_addr  SPI Flash 内部起始地址。
 * @param buffer     接收数据的目标 RAM 缓冲区。
 * @param length     需要读取的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Read(uint32_t read_addr, uint8_t *buffer, uint32_t length);

/**
 * @brief 擦除并写入数据到 SPI Flash。
 * @param write_addr SPI Flash 内部起始地址。
 * @param buffer     待写入的源数据缓冲区。
 * @param length     需要写入的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Write(uint32_t write_addr, const uint8_t *buffer, uint32_t length);

/**
 * @brief 擦除 SPI Flash 中的一段字节区间。
 * @param erase_addr SPI Flash 内部起始地址。
 * @param length     需要擦除的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Erase(uint32_t erase_addr, uint32_t length);

#endif
