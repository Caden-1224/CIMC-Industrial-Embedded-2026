#include "Spi_flash_service.h"
#include "SPI_FLASH.h"

typedef struct
{
    uint32_t jedec_id;
    uint8_t is_ready;
} SpiFlash_ServiceState_t;

static SpiFlash_ServiceState_t s_spi_flash_service;

/**
 * @brief 验证 JEDEC ID 是否看起来像在线 Flash 器件的有效值。
 * @param jedec_id 底层驱动返回的 24 位 JEDEC ID。
 * @return uint8_t ID 可用返回 1U，否则返回 0U。
 */
static uint8_t SpiFlash_Service_IsValidId(uint32_t jedec_id)
{
    if ((jedec_id == 0U) || (jedec_id == 0x00FFFFFFUL) || (jedec_id == 0xFFFFFFFFUL))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 在访问 Flash 驱动之前检查通用请求前置条件。
 * @param buffer 调用方提供的数据缓冲区。
 * @param length 请求的字节数。
 * @return uint8_t 请求有效返回 1U，否则返回 0U。
 */
static uint8_t SpiFlash_Service_IsValidRequest(const void *buffer, uint32_t length)
{
    if ((s_spi_flash_service.is_ready == 0U) || (buffer == NULL) || (length == 0U))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 初始化 SPI Flash 服务并缓存检测到的 JEDEC ID。
 * @param void 无输入参数。
 * @return void 无返回值。
 */
void SpiFlash_Service_Init(void)
{
    spi_flash_init();

    s_spi_flash_service.jedec_id = spi_flash_read_id();
    s_spi_flash_service.is_ready = SpiFlash_Service_IsValidId(s_spi_flash_service.jedec_id);
}

/**
 * @brief 查询 SPI Flash 服务是否已就绪，可以正常访问。
 * @param void 无输入参数。
 * @return uint8_t 就绪返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_IsReady(void)
{
    return s_spi_flash_service.is_ready;
}

/**
 * @brief 获取服务初始化时缓存的 JEDEC ID。
 * @param void 无输入参数。
 * @return uint32_t 缓存的 24 位 JEDEC ID。
 */
uint32_t SpiFlash_Service_GetId(void)
{
    return s_spi_flash_service.jedec_id;
}

/**
 * @brief 从 SPI Flash 读取数据。
 * @param read_addr SPI Flash 内部起始地址。
 * @param buffer    目标 RAM 缓冲区。
 * @param length    需要读取的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Read(uint32_t read_addr, uint8_t *buffer, uint32_t length)
{
    if (SpiFlash_Service_IsValidRequest(buffer, length) == 0U)
    {
        return 0U;
    }

    spi_flash_buffer_read(buffer, read_addr, length);
    return 1U;
}

/**
 * @brief 通过底层保护擦除辅助函数擦除一段字节区间。
 * @param erase_addr SPI Flash 内部起始地址。
 * @param length     需要擦除的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Erase(uint32_t erase_addr, uint32_t length)
{
    if ((s_spi_flash_service.is_ready == 0U) || (length == 0U))
    {
        return 0U;
    }

    spi_flash_buffer_erase(erase_addr, length);
    return 1U;
}

/**
 * @brief 擦除并写入一段数据到 SPI Flash。
 * @param write_addr SPI Flash 内部起始地址。
 * @param buffer     源数据缓冲区。
 * @param length     需要写入的字节数。
 * @return uint8_t 请求被接受返回 1U，否则返回 0U。
 */
uint8_t SpiFlash_Service_Write(uint32_t write_addr, const uint8_t *buffer, uint32_t length)
{
    if (SpiFlash_Service_IsValidRequest(buffer, length) == 0U)
    {
        return 0U;
    }

    spi_flash_buffer_erase(write_addr, length);
    spi_flash_buffer_write(buffer, write_addr, length);
    return 1U;
}
