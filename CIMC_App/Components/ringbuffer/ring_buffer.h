#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 环形缓冲区控制结构体。
 */
typedef struct {
    /**< 外部提供的缓冲区地址 */
    uint8_t *buffer;
    /**< 缓冲区总大小，单位字节；实际可用容量为 size - 1 */
    uint32_t size;
    /**< 写入位置，指向下一次写入的下标 */
    uint32_t head;
    /**< 读取位置，指向下一次读取的下标 */
    uint32_t tail;
} ring_buffer_t;

/**
 * @brief 初始化环形缓冲区。
 *
 * 将 ring 绑定到外部提供的 buffer，并清空读写位置。buffer 的生命周期必须
 * 覆盖 ring_buffer_t 的使用周期。
 *
 * @param[in,out] ring 环形缓冲区控制结构体
 * @param[in] buffer 外部提供的数据缓冲区
 * @param[in] size buffer 的大小，单位字节
 * @return 无
 */
void ring_buffer_init(ring_buffer_t *ring, uint8_t *buffer, uint32_t size);

/**
 * @brief 获取当前可读取的数据长度。
 *
 * @param[in] ring 环形缓冲区控制结构体
 * @return 当前已经写入且尚未读取的数据长度，单位字节
 */
uint32_t ring_buffer_available(const ring_buffer_t *ring);

/**
 * @brief 获取当前剩余可写空间。
 *
 * 环形缓冲区会保留 1 字节用于区分空/满状态，因此最大可写容量为 size - 1。
 *
 * @param[in] ring 环形缓冲区控制结构体
 * @return 当前还能写入的数据长度，单位字节
 */
uint32_t ring_buffer_free(const ring_buffer_t *ring);

/**
 * @brief 向环形缓冲区写入数据。
 *
 * 如果剩余空间不足，本函数不会写入部分数据，而是直接返回 false。
 *
 * @param[in,out] ring 环形缓冲区控制结构体
 * @param[in] data 待写入的数据指针
 * @param[in] len 待写入的数据长度，单位字节
 * @return
 *      - true 写入成功
 *      - false 参数无效或剩余空间不足
 */
bool ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, uint32_t len);

/**
 * @brief 预读环形缓冲区数据但不移动读取位置。
 *
 * 实际读取长度会被限制在当前可读数据长度以内。
 *
 * @param[in] ring 环形缓冲区控制结构体
 * @param[out] data 用于接收数据的缓冲区
 * @param[in] len 希望预读的数据长度，单位字节
 * @return 实际预读的数据长度，单位字节
 */
uint32_t ring_buffer_peek(const ring_buffer_t *ring, uint8_t *data, uint32_t len);

/**
 * @brief 从环形缓冲区读取数据并移动读取位置。
 *
 * 实际读取长度会被限制在当前可读数据长度以内。
 *
 * @param[in,out] ring 环形缓冲区控制结构体
 * @param[out] data 用于接收数据的缓冲区
 * @param[in] len 希望读取的数据长度，单位字节
 * @return 实际读取的数据长度，单位字节
 */
uint32_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, uint32_t len);

/**
 * @brief 丢弃指定长度的数据。
 *
 * 用于协议解析时跳过无效字节或已经处理过的头部数据。
 *
 * @param[in,out] ring 环形缓冲区控制结构体
 * @param[in] len 希望丢弃的数据长度，单位字节
 * @return 无
 */
void ring_buffer_drop(ring_buffer_t *ring, uint32_t len);

/**
 * @brief 清空环形缓冲区。
 *
 * 只复位读写位置，不会主动清零底层 buffer 内容。
 *
 * @param[in,out] ring 环形缓冲区控制结构体
 * @return 无
 */
void ring_buffer_clear(ring_buffer_t *ring);

#endif /* RING_BUFFER_H */
