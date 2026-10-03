/************************************************************
 * 版权所有：2025 CIMC Copyright
 * 文件名称：SPI_FLASH.h
 * 作    者：Jianchuan Wang / Tao Liu @ GigaDevice
 * 维护作者：Caden / Codex
 * 版    本：V0.02
 * 说    明：板载 SPI Flash 硬件驱动对外接口声明。
 ************************************************************/

#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "HeaderFiles.h"

/* SPI Nor Flash 的单页编程大小固定为 256 字节，超过这个长度就必须拆页写。 */
#define SPI_FLASH_PAGE_SIZE   ((uint32_t)256U)

/* SPI Nor Flash 的最小擦除粒度通常是 4KB 扇区，本工程也按这个粒度工作。 */
#define SPI_FLASH_SECTOR_SIZE ((uint32_t)4096U)

/* 把片选线拉低，等价于告诉 Flash：“接下来总线上的字节是发给你的”。 */
#define SPI_FLASH_CS_LOW()  gpio_bit_reset(GPIOA, GPIO_PIN_15)

/* 把片选线拉高，等价于告诉 Flash：“这次 SPI 事务结束了，你可以开始内部执行了”。 */
#define SPI_FLASH_CS_HIGH() gpio_bit_set(GPIOA, GPIO_PIN_15)

/**
 * @brief 初始化板载 SPI Flash 所依赖的 GPIO 和 SPI0 外设。
 * @param void 无入参。
 * @return void 无返回值。
 * @details
 * 这个函数只负责把 MCU 侧的通信通道搭起来，
 * 包括时钟、GPIO 复用、SPI 模式、片选脚初始电平等。
 * 它不会主动读写 Flash 数据，只是为后续访问做好准备。
 */
void spi_flash_init(void);

/**
 * @brief 擦除目标地址所在的整个 4KB 扇区。
 * @param sector_addr 目标地址，可以传这个扇区中的任意地址。
 * @return void 无返回值。
 * @details
 * Nor Flash 不能像 RAM 一样随便把某几个字节“清零”，
 * 真正的硬件擦除必须按扇区粒度执行。
 * 这个接口会先把地址规整到扇区首地址，再发送扇区擦除命令。
 */
void spi_flash_sector_erase(uint32_t sector_addr);

/**
 * @brief 擦除整片 SPI Flash。
 * @param void 无入参。
 * @return void 无返回值。
 * @details
 * 这是风险最高、耗时也最长的操作，
 * 调用后整片芯片里的用户数据都会被恢复为 0xFF。
 */
void spi_flash_bulk_erase(void);

/**
 * @brief 在同一页内连续写入若干字节。
 * @param pbuffer 指向待写入数据缓冲区的指针。
 * @param write_addr Flash 内部起始写地址，要求不能跨页。
 * @param num_byte_to_write 本次实际写入的字节数。
 * @return void 无返回值。
 * @details
 * Page Program 命令一次最多写 256 字节，
 * 而且不能跨页，否则数据会从页首回卷，导致内容错乱。
 * 所以这个函数只处理“单页内”的那一小段数据。
 */
void spi_flash_page_write(const uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write);

/**
 * @brief 自动拆页并写入任意长度的数据。
 * @param pbuffer 指向待写入数据缓冲区的指针。
 * @param write_addr Flash 内部起始写地址。
 * @param num_byte_to_write 需要写入的总字节数。
 * @return void 无返回值。
 * @details
 * 这是对 `spi_flash_page_write()` 的进一步封装。
 * 上层只要给出起始地址和总长度，
 * 这个函数会自动算出每一页还能写多少，并循环写完整段数据。
 */
void spi_flash_buffer_write(const uint8_t *pbuffer, uint32_t write_addr, uint32_t num_byte_to_write);

/**
 * @brief 从 Flash 中读取连续数据到目标缓冲区。
 * @param pbuffer 接收读取结果的 RAM 缓冲区。
 * @param read_addr Flash 内部起始读取地址。
 * @param num_byte_to_read 需要读取的字节数。
 * @return void 无返回值。
 * @details
 * 标准读命令没有页边界限制，
 * 所以读取时只要地址和长度合法，就可以连续读出整段数据。
 */
void spi_flash_buffer_read(uint8_t *pbuffer, uint32_t read_addr, uint32_t num_byte_to_read);

/**
 * @brief 读取当前 Flash 的 JEDEC ID。
 * @param void 无入参。
 * @return uint32_t 返回 24 位 JEDEC ID，高字节在前。
 * @details
 * JEDEC ID 常用于识别芯片厂商、器件类型和容量编码，
 * 是最常见的“器件在线检测”手段之一。
 */
uint32_t spi_flash_read_id(void);

/**
 * @brief 启动一次连续读取序列，但暂时不释放片选。
 * @param read_addr 连续读取的起始地址。
 * @return void 无返回值。
 * @details
 * 如果上层希望先发读命令，再多次逐字节拉数据，
 * 可以先调用这个函数建立事务，
 * 然后重复调用 `spi_flash_read_byte()`。
 */
void spi_flash_start_read_sequence(uint32_t read_addr);

/**
 * @brief 在已建立好的连续读事务中继续读取 1 个字节。
 * @param void 无入参。
 * @return uint8_t 返回当前读取到的 1 个字节。
 * @details
 * SPI 是同步串行总线，主机必须继续送时钟，Flash 才会继续吐数据。
 * 所以“读 1 字节”的本质，其实是“发送 1 个占位字节并接收返回值”。
 */
uint8_t spi_flash_read_byte(void);

/**
 * @brief 通过 SPI0 发送 1 个字节，并取回同时收到的 1 个字节。
 * @param byte 需要发出的字节。
 * @return uint8_t 本次 SPI 收发同步得到的返回字节。
 * @details
 * SPI 是全双工总线，
 * 所以“发送”和“接收”永远同时发生。
 * 这个函数是整个 Flash 驱动最基础的字节级收发原语。
 */
uint8_t spi_flash_send_byte(uint8_t byte);

/**
 * @brief 通过 SPI0 发送 1 个半字，并取回同时收到的 1 个半字。
 * @param half_word 需要发出的 16 位数据。
 * @return uint16_t 本次 16 位 SPI 传输收到的数据。
 * @details
 * 当前工程把 SPI 配置成了 8bit 帧格式，
 * 这个接口主要是为了兼容原模板而保留，
 * 默认流程里并没有真正使用它。
 */
uint16_t spi_flash_send_halfword(uint16_t half_word);

/**
 * @brief 发送写使能命令。
 * @param void 无入参。
 * @return void 无返回值。
 * @details
 * 大多数 Nor Flash 在上电后都会默认禁止写操作，
 * 每次执行写入或擦除前，都要先发一次 WREN 命令把 WEL 位置 1。
 */
void spi_flash_write_enable(void);

/**
 * @brief 轮询等待当前写入或擦除操作完成。
 * @param void 无入参。
 * @return void 无返回值。
 * @details
 * Flash 的写入和擦除都是“命令先发完，芯片再内部慢慢执行”，
 * 所以后续操作前必须轮询状态寄存器里的 WIP 位，
 * 确认它已经不忙了。
 */
void spi_flash_wait_for_write_end(void);

/**
 * @brief 擦除任意长度的数据区间，并保护同扇区内未命中的旧数据。
 * @param sector_addr 擦除起始地址，可以不按扇区对齐。
 * @param num_byte_to_erase 需要擦除的总字节数。
 * @return void 无返回值。
 * @details
 * 这是一个“范围擦除”辅助接口。
 * 因为底层硬件只能按整个扇区擦，
 * 所以当调用者只想擦掉扇区中的一小段时，
 * 这里会执行“先整扇区读出到 RAM -> 把目标范围改成 0xFF -> 擦整扇区 -> 把要保留的数据写回”的保护流程。
 */
void spi_flash_buffer_erase(uint32_t sector_addr, uint32_t num_byte_to_erase);

#endif
