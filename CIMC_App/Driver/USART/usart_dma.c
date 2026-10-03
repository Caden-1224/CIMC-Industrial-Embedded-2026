/************************************************************
 * 文件：usart_dma.c
 * 说明：GD32 USART0 —�?DMA 收发 + IDLE 空闲检测分�? *
 * 工作原理（整体数据流）：
 *
 *   发送方向：
 *     用户调用 usart_dma_send_bytes()
 *     �?把用户数据复制到 s_dma_tx_buffer[]
 *     �?配置 DMA 通道：从 s_dma_tx_buffer 搬到 USART_DATA 寄存�? *     �?启动 DMA，硬件自动逐字节搬运（CPU 不用管）
 *     �?等待 DMA 传输完成标志（FTF�? *     �?等待 USART TC（最后一个字节从移位寄存器发出）
 *     �?返回
 *
 *   接收方向�? *     DMA 通道配置为：�?USART_DATA 寄存�?�?搬到 s_dma_rx_buffer[]
 *     DMA 一直在后台自动搬运，每收到 1 字节就自动存�?s_dma_rx_buffer
 *     CPU 完全不参与接收过�?�?这就�?DMA 的核心优�? *
 *     当总线空闲一个字节时�?�?触发 IDLE 中断
 *     �?USART0_IRQHandler 里：
 *         1. 暂停 DMA 接收通道
 *         2. 通过"总容�?- 剩余数量"算出收到了多少字�? *         3. �?s_dma_rx_buffer 的数据搬�?s_frame_buffer
 *         4. 重启 DMA 接收通道（重新配置地址和数量）
 *     �?主循环调�?usart_dma_fetch_frame() 取走帧数�? *
 * �?IDLE 模式的关键区别：
 *   IDLE 模式：每个字节都要进一�?RBNE 中断 �?CPU 开销�? *   DMA  模式：接收零 CPU 开销，只�?IDLE 时进一次中�?�?CPU 开销极小
 *   代价�?DMA 模式需要占�?DMA 控制器的两个通道�? *
 * 编译守卫�? *   整个文件�?#if (USART0_RX_MODE == USART0_MODE_DMA) 包裹�? *   只有用户�?USART.h 里选了 DMA 模式时才参与编译�? ************************************************************/

#include "usart_dma.h"

#if (USART0_RX_MODE == USART0_MODE_DMA)

/* ============================================================
 * 静态变�? * ============================================================ */

/**
 * s_dma_tx_buffer[] �?DMA 发送缓冲区
 *
 * DMA 发送时需要一�?源地址"，CPU 先把待发送数据复制到这里�? * 然后告诉 DMA "从这个地址开始往 USART_DATA 寄存器搬 len 个字�?�? * 不直接用用户�?data 指针，是因为 DMA 传输期间用户数据可能被修改�? * 大小�?USART0_TX_BUFFER_SIZE 决定（默�?256 字节）�? */
static uint8_t           s_dma_tx_buffer[USART0_TX_BUFFER_SIZE];

/**
 * s_dma_rx_buffer[] �?DMA 接收缓冲�? *
 * DMA 接收通道配置好后，硬件会自动�?USART_DATA 寄存器收到的新字�? * 按顺序写入这个数组。大小由 USART0_RX_BUFFER_SIZE 决定（默�?512 字节）�? * IDLE 中断触发时，通过 DMA 剩余传输数算出实际收到了多少字节�? */
static uint8_t           s_dma_rx_buffer[USART0_RX_BUFFER_SIZE];

/**
 * s_frame_buffer[] �?完整帧缓冲区（与 IDLE 模式作用相同�? *
 * IDLE 中断里把 s_dma_rx_buffer 的数据搬到这里，
 * �?DMA 接收缓冲区隔离，避免主循环读到半成品数据�? */
static volatile uint8_t  s_frame_buffer[USART0_FRAME_BUFFER_SIZE];

/** s_frame_length �?当前帧的实际字节�?*/
static volatile uint16_t s_frame_length = 0U;

/** s_frame_ready �?帧就绪标志：0=无帧�?=有新帧等待取�?*/
static volatile uint8_t  s_frame_ready  = 0U;

/**
 * s_dma_tx_busy �?DMA 发送忙标志
 *
 *   1 = DMA 正在搬运发送数据，不能重配通道
 *   0 = 空闲，可以启动新的发�? * 当前实现�?send_bytes 内部已经阻塞等待完成�? * 这个标志主要是防止重入和给未来非阻塞发送预留�? */
static volatile uint8_t  s_dma_tx_busy  = 0U;

static volatile uint8_t  s_raw_stream_active = 0U;
static volatile uint32_t s_raw_stream_expected_total = 0UL;
static volatile uint8_t  s_raw_stream_holding_frame = 0U;

/* ============================================================
 * DMA 通道初始�? *
 * GD32F4xx �?DMA 支持两种传输模式�? *   - Single Data（单数据）：每完成一次传输产生一次请求，适合外设
 *   - Multi Block（多块）：FIFO 模式，适合存储器间大块搬运
 *
 * 串口是单字节外设，所以用 Single Data 模式�? * ============================================================ */

/**
 * @brief  初始�?DMA 发送通道（不启动传输，只配置参数模板）�? *
 * DMA 发送的数据流向�? *   内存（s_dma_tx_buffer�?�?外设（USART_DATA 寄存器）
 *
 * 配置要点�? *   - direction = MEMORY_TO_PERIPH：方向是 内存→外�? *   - memory_inc = ENABLE：每搬一个字节，内存地址 +1（逐字节取数据�? *   - periph_inc = DISABLE：外设地址不变（始终写同一�?USART_DATA 寄存器）
 *   - circular = DISABLE：Normal 模式，搬完指定数量后自动停止
 *   - number = 0：这里先�?0，真正发送时再通过 dma_transfer_number_config 设定
 */
static void usart_dma_tx_channel_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    /* 先复位通道到默认状�?*/
    dma_deinit(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL);

    /* 用库函数把结构体所有字段填为默认值，防止野�?*/
    dma_single_data_para_struct_init(&dma_init_struct);

    /* 逐项设置 DMA 参数 */
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;   /* 内存 �?外设 */
    dma_init_struct.memory0_addr        = (uint32_t)s_dma_tx_buffer; /* 内存源地址 */
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE; /* 内存地址自增 */
    dma_init_struct.number              = 0U;                     /* 传输数量（发送时再设�?*/
    dma_init_struct.periph_addr         = USART_DMA_DATA_ADDR;    /* 外设目标地址 = USART_DATA */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE; /* 外设地址不变 */
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;  /* 每次传输 8 �?*/
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;/* 通道优先级最�?*/
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE; /* Normal 模式 */

    /* 把上述配置写�?DMA 寄存�?*/
    dma_single_data_mode_init(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, &dma_init_struct);

    /* 选择 DMA 子外设通道（USART0 �?DMA1 �?subperipheral 4 上） */
    dma_channel_subperipheral_select(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, USART_DMA_SUBPERI);
}

/**
 * @brief  初始�?DMA 接收通道并立即启动�? *
 * DMA 接收的数据流向：
 *   外设（USART_DATA 寄存器） �?内存（s_dma_rx_buffer�? *
 * 与发送通道的区别：
 *   - direction = PERIPH_TO_MEMORY：方向是 外设→内�? *   - number = USART0_RX_BUFFER_SIZE：直接设满，DMA 会从第一个字节开始填
 *   - 最后调�?dma_channel_enable 立即启动接收
 *     （发送通道不启动，等用户调�?send_bytes 时才启动�? */
static void usart_dma_rx_channel_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_deinit(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);
    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;   /* 外设 �?内存 */
    dma_init_struct.memory0_addr        = (uint32_t)s_dma_rx_buffer; /* 内存目标地址 */
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number              = USART0_RX_BUFFER_SIZE;   /* 传输数量 = 缓冲区满 */
    dma_init_struct.periph_addr         = USART_DMA_DATA_ADDR;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;

    dma_single_data_mode_init(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL, USART_DMA_SUBPERI);

    /* 接收通道：配置完立即启动，DMA 在后台持续等�?USART 数据 */
    dma_channel_enable(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);
}

/* ============================================================
 * 初始化函�? * ============================================================ */

/**
 * @brief  初始�?USART0 �?DMA 收发资源�? *
 * 初始化顺序：
 *   1. 开时钟（GPIO + USART + DMA�? *   2. �?GPIO 引脚复用
 *   3. �?USART 通信参数
 *   4. 初始�?DMA 发�?接收通道
 *   5. �?USART 发送器和接收器连接�?DMA
 *   6. �?NVIC，开�?IDLE 中断
 *   7. 使能 USART
 *
 * �?IDLE 模式的区别：
 *   - 不开�?RBNE 中断（接收由 DMA 完成，不需�?CPU 逐字节中断）
 *   - 多了 DMA 通道初始化和 USART-DMA 连接
 *
 * @param  baudrate: 串口波特率，�?115200U�? */
void usart_dma_init(uint32_t baudrate)
{
    /* --- �?1 步：开时钟（比 IDLE 模式多了一�?DMA 时钟�?--- */
    rcu_periph_clock_enable(USART0_GPIO_RCU);
    rcu_periph_clock_enable(USART0_USART_RCU);
    rcu_periph_clock_enable(USART_DMA_RCU);       /* DMA1 时钟 */

    /* --- �?2 步：GPIO 引脚复用（与 IDLE 模式完全相同�?--- */
    gpio_af_set(USART0_PORT, USART0_GPIO_AF, USART0_TX_PIN | USART0_RX_PIN);
    gpio_mode_set(USART0_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  USART0_TX_PIN | USART0_RX_PIN);
    gpio_output_options_set(USART0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            USART0_TX_PIN | USART0_RX_PIN);

    /* --- �?3 步：USART 通信参数（与 IDLE 模式完全相同�?--- */
    usart_deinit(USART0_PERIPH);
    usart_word_length_set(USART0_PERIPH, USART_WL_8BIT);
    usart_stop_bit_set(USART0_PERIPH, USART_STB_1BIT);
    usart_parity_config(USART0_PERIPH, USART_PM_NONE);
    usart_baudrate_set(USART0_PERIPH, baudrate);
    usart_transmit_config(USART0_PERIPH, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART0_PERIPH, USART_RECEIVE_ENABLE);

    /* --- �?4 步：初始�?DMA 通道 --- */
    usart_dma_tx_channel_init();    /* 发送通道（只配置，不启动�?*/
    usart_dma_rx_channel_init();    /* 接收通道（配�?+ 立即启动�?*/

    /* --- �?5 步：�?USART 发�?接收器连接到 DMA ---
     * 这一步让 USART 在需要收发数据时，不触发 CPU 中断�?     * 而是直接�?DMA 控制器发送请求�?*/
    usart_dma_transmit_config(USART0_PERIPH, USART_TRANSMIT_DMA_ENABLE);
    usart_dma_receive_config(USART0_PERIPH, USART_RECEIVE_DMA_ENABLE);

    /* --- �?6 步：NVIC + IDLE 中断 ---
     * 注意：DMA 模式下不需�?RBNE 中断，接收完全由 DMA 完成�?     * 只需�?IDLE 中断来检�?一帧结�?�?*/
    nvic_irq_enable(USART0_IRQn, 3U, 2U);
    usart_interrupt_enable(USART0_PERIPH, USART_INT_IDLE);

    /* --- �?7 步：使能 USART --- */
    usart_enable(USART0_PERIPH);
}

/* ============================================================
 * 发送函�? * ============================================================ */

/**
 * @brief  通过 DMA 发送一段数据（阻塞等待完成）�? *
 * 完整发送流程：
 *   1. 参数检�?+ 长度截断（不超过发送缓冲区大小�? *   2. 等上一�?DMA 发送完成（s_dma_tx_busy == 0�? *   3. 把用户数据复制到 s_dma_tx_buffer
 *   4. 关闭 DMA 通道 �?清除旧标�?�?重配内存地址和传输数�?�?重新开�? *   5. �?DMA 传输完成标志（FTF = Full Transfer Finished�? *   6. �?USART TC 标志（最后一个字节从移位寄存器真正发出）
 *   7. 清标志，解除忙状�? *
 * @param  data: 待发送数据首地址�? * @param  len:  待发送字节数�? */
void usart_dma_send_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t index = 0U;

    /* 防御性检�?*/
    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    /* 超过发送缓冲区容量则截�?*/
    if (len > USART0_TX_BUFFER_SIZE)
    {
        len = USART0_TX_BUFFER_SIZE;
    }

    /* 等待上一次发送完成（防止重入�?*/
    while (s_dma_tx_busy != 0U)
    {
    }

    /* 标记发送忙 */
    s_dma_tx_busy = 1U;

    /* 把用户数据复制到 DMA 发送缓冲区
     * 为什么不直接�?data 指针�?DMA�?     *   1. data 指向的内存可能在 Flash 上，某些 DMA 配置对源地址有对齐要�?     *   2. 用户可能�?DMA 传输期间修改�?data 指向的内�?     *   3. 统一�?s_dma_tx_buffer 方便管理地址对齐 */
    for (index = 0U; index < len; index++)
    {
        s_dma_tx_buffer[index] = data[index];
    }

    /*
     * 重配 DMA 发送通道（标准四步操作）�?     *   1. 关闭通道（配置前必须关闭，否则部分寄存器写不进去�?     *   2. 清除上次传输的完成标志（防止立即检测到旧标志）
     *   3. 重设内存地址和传输数�?     *   4. 重新开启通道，DMA 开始搬�?     */
    dma_channel_disable(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL);
    dma_flag_clear(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    dma_memory_address_config(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL,
                              DMA_MEMORY_0, (uint32_t)s_dma_tx_buffer);
    dma_transfer_number_config(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, len);
    dma_channel_enable(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL);

    /* 等待 DMA 全部搬运完成
     * FTF（Full Transfer Finished）：DMA 把指定数量的字节全部搬完后置 1 */
    while (dma_flag_get(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, DMA_FLAG_FTF) == RESET)
    {
    }

    /* 等待 USART TC：DMA 搬完只代表数据都到了 USART 数据寄存器，
     * 但最后一个字节可能还在移位寄存器里往外发�?     * �?TC 确保线路真正空闲后再返回�?*/
    while (usart_flag_get(USART0_PERIPH, USART_FLAG_TC) == RESET)
    {
    }

    /* 清除 DMA 完成标志，为下一次发送做准备 */
    dma_flag_clear(USART_DMA_TX_DMA, USART_DMA_TX_CHANNEL, DMA_FLAG_FTF);

    /* 解除忙标�?*/
    s_dma_tx_busy = 0U;
}

/* ============================================================
 * 帧读取函�? * ============================================================ */

/**
 * @brief  从帧缓冲区中取出一帧完整数据�? *
 * 逻辑�?IDLE 模式�?fetch_frame 完全一致：
 *   检�?s_frame_ready �?复制�?out_buf �?清标�?�?返回�? *
 * @param  out_buf: 用户输出缓冲区�? * @param  out_len: 输出帧长度指针�? * @param  max_len: out_buf 最大容量�? * @return 1 = 取到一帧，0 = 无帧�? */
uint8_t usart_dma_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
{
    uint16_t copy_len = 0U;
    uint16_t index    = 0U;

    if ((out_buf == NULL) || (out_len == NULL))
    {
        return 0U;
    }

    if (s_frame_ready == 0U)
    {
        return 0U;
    }

    copy_len = s_frame_length;
    if (copy_len > max_len)
    {
        copy_len = max_len;
    }

    for (index = 0U; index < copy_len; index++)
    {
        out_buf[index] = s_frame_buffer[index];
    }

    *out_len       = copy_len;
    s_frame_length = 0U;
    s_frame_ready  = 0U;


    if (s_raw_stream_holding_frame != 0U)
    {
        s_raw_stream_holding_frame = 0U;
        dma_memory_address_config(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL,
                                  DMA_MEMORY_0, (uint32_t)s_dma_rx_buffer);
        dma_transfer_number_config(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL,
                                   USART0_RX_BUFFER_SIZE);
        dma_flag_clear(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL, DMA_FLAG_FTF);
        dma_channel_enable(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);
    }
    return 1U;
}

/**
 * @brief  查询 DMA 发送是否忙碌�? * @return 1 = 正在发送，0 = 空闲�? */
uint8_t usart_dma_is_busy(void)
{
    return s_dma_tx_busy;
}

/* ============================================================
 * 中断服务函数
 *
 * DMA 模式下，接收�?DMA 硬件完成，不需�?RBNE 中断�? * 唯一需要中断处理的�?IDLE 事件：检测到一帧结束后�? * 暂停 DMA �?算长�?�?搬数�?�?重启 DMA�? * ============================================================ */

/**
 * @brief  USART0 中断服务函数（DMA 模式专用）�? *
 * IDLE 触发后的处理流程�? *   1. �?USART_DATA 清除 IDLE 标志（手册要求）
 *   2. 关闭 DMA 接收通道（暂停接收，防止在搬运期间新数据覆盖�? *   3. 计算已接收字节数 = 总容�?- DMA 剩余传输数量
 *      例：配了 512 字节，DMA 剩余 489 �?已收 23 字节
 *   4. �?s_dma_rx_buffer 的数据搬�?s_frame_buffer
 *   5. 置位 s_frame_ready
 *   6. 重启 DMA 接收通道（重设地址和数量，从缓冲区头部开始）
 */
void USART0_IRQHandler(void)
{
    uint32_t received_length = 0UL;
    uint32_t copy_length = 0UL;
    uint32_t image_size = 0UL;
    uint32_t index = 0UL;

    if (usart_interrupt_flag_get(USART0_PERIPH, USART_INT_FLAG_IDLE) != RESET)
    {
        (void)USART_STAT0(USART0_PERIPH);
        (void)USART_DATA(USART0_PERIPH);

        received_length = (uint32_t)USART0_RX_BUFFER_SIZE -
                          dma_transfer_number_get(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);

        if ((s_raw_stream_active != 0U) ||
            ((received_length >= 4UL) &&
             (s_dma_rx_buffer[0] == 0x5AU) &&
             (s_dma_rx_buffer[1] == 0xA5U) &&
             (s_dma_rx_buffer[2] == 0xC3U) &&
             (s_dma_rx_buffer[3] == 0x3CU)))
        {
            s_raw_stream_active = 1U;

            if ((s_raw_stream_expected_total == 0UL) && (received_length >= 16UL))
            {
                image_size = ((uint32_t)s_dma_rx_buffer[8]) |
                             ((uint32_t)s_dma_rx_buffer[9] << 8) |
                             ((uint32_t)s_dma_rx_buffer[10] << 16) |
                             ((uint32_t)s_dma_rx_buffer[11] << 24);

                if ((image_size > 0UL) &&
                    ((image_size + 16UL) <= USART0_FRAME_BUFFER_SIZE) &&
                    ((image_size + 16UL) <= USART0_RX_BUFFER_SIZE))
                {
                    s_raw_stream_expected_total = image_size + 16UL;
                }
                else
                {
                    s_raw_stream_active = 0U;
                    s_raw_stream_expected_total = 0UL;
                }
            }

            if ((s_raw_stream_expected_total != 0UL) &&
                (received_length >= s_raw_stream_expected_total))
            {
                dma_channel_disable(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);
                copy_length = s_raw_stream_expected_total;

                for (index = 0UL; index < copy_length; index++)
                {
                    s_frame_buffer[index] = s_dma_rx_buffer[index];
                }

                s_frame_length = (uint16_t)copy_length;
                s_frame_ready = 1U;
                s_raw_stream_active = 0U;
                s_raw_stream_expected_total = 0UL;
                s_raw_stream_holding_frame = 1U;

            }

            return;
        }

        dma_channel_disable(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);

        if (received_length > USART0_FRAME_BUFFER_SIZE)
        {
            received_length = USART0_FRAME_BUFFER_SIZE;
        }

        for (index = 0UL; index < received_length; index++)
        {
            s_frame_buffer[index] = s_dma_rx_buffer[index];
        }

        s_frame_length = (uint16_t)received_length;
        s_frame_ready = (received_length != 0UL) ? 1U : 0U;

        dma_memory_address_config(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL,
                                  DMA_MEMORY_0, (uint32_t)s_dma_rx_buffer);
        dma_transfer_number_config(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL,
                                   USART0_RX_BUFFER_SIZE);
        dma_flag_clear(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL, DMA_FLAG_FTF);
        dma_channel_enable(USART_DMA_RX_DMA, USART_DMA_RX_CHANNEL);
    }
}

#endif /* USART0_RX_MODE == USART0_MODE_DMA */
