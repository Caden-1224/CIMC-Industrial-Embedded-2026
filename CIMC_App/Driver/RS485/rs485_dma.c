/************************************************************
 * 文件：rs485_dma.c
 * 说明：GD32 RS485 (USART1) —— DMA 收发 + IDLE 空闲检测分帧
 *
 * 与 usart_dma.c 的关系：
 *   接收逻辑结构完全一致（DMA 后台搬运，IDLE 触发分帧）。
 *   差别有三：
 *     1. 外设由 USART0 换成 USART1，DMA 控制器从 DMA1 换成 DMA0
 *     2. 发送时必须配合 DE 引脚切换收发方向
 *     3. DMA 发送完成后，必须再等 USART_FLAG_TC 才能拉低 DE
 *        —— FTF 只代表数据搬到了 USART_DATA 寄存器，不代表已经发完到线上
 *
 * 编译守卫：
 *   整个文件被 #if (RS485_RX_MODE == RS485_MODE_DMA) 包裹，
 *   只有用户在 RS485.h 里选了 DMA 模式时才会参与编译。
 ************************************************************/

#include "rs485_dma.h"

#if (RS485_RX_MODE == RS485_MODE_DMA)

/* ============================================================
 * 静态变量
 * ============================================================ */

/** DMA 发送缓冲区。
 *  调用者传进来的 data 可能在 Flash 上或随时被改写，统一拷贝到这里再让 DMA 搬。
 *  容量 256 与 RS485_FRAME_BUFFER_SIZE 一致，单次发送不超过这个长度。 */
static uint8_t s_dma_tx_buffer[RS485_FRAME_BUFFER_SIZE];

/** DMA 接收缓冲区。
 *  DMA 后台持续把 USART_DATA 收到的字节按顺序写到这里，
 *  IDLE 触发后通过"总容量 - DMA 剩余传输数"算出实际收了多少字节。 */
static uint8_t s_dma_rx_buffer[RS485_RX_BUFFER_SIZE];

/** 完整帧缓冲区，与 DMA 接收缓冲区隔离，防止主循环读到半成品。 */
static volatile uint8_t  s_frame_buffer[RS485_FRAME_BUFFER_SIZE];

/** 完整帧的实际字节数 */
static volatile uint16_t s_frame_length = 0U;

/** 帧就绪标志：0 = 无帧，1 = 有新帧等待主循环取走 */
static volatile uint8_t  s_frame_ready = 0U;

/** DMA 发送忙标志：1 = DMA 正在搬运，0 = 空闲 */
static volatile uint8_t  s_dma_tx_busy = 0U;

/* ============================================================
 * DMA 通道初始化
 * ============================================================ */

/**
 * 初始化 DMA0 CH6（发送）。只配置参数模板，不启动通道。
 * 真正发送时再通过 dma_transfer_number_config 设置本次长度并 enable。
 */
static void rs485_dma_tx_channel_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_deinit(DMA0, RS485_DMA_TX_CHANNEL);
    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;          /* 内存 → 外设 */
    dma_init_struct.memory0_addr        = (uint32_t)s_dma_tx_buffer;     /* 源：发送缓冲区 */
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;    /* 每搬一字节 mem 地址 +1 */
    dma_init_struct.number              = 0U;                            /* 长度发送时再设 */
    dma_init_struct.periph_addr         = (uint32_t)&USART_DATA(USART1); /* 目的：USART1_DATA */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;   /* 外设地址固定 */
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;     /* 单次模式 */

    dma_single_data_mode_init(DMA0, RS485_DMA_TX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(DMA0, RS485_DMA_TX_CHANNEL, DMA_SUBPERI4);
}

/**
 * 初始化 DMA0 CH5（接收）并立即启动。
 * 启动后 DMA 在后台持续等待 USART1 的接收事件。
 */
static void rs485_dma_rx_channel_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_deinit(DMA0, RS485_DMA_RX_CHANNEL);
    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;          /* 外设 → 内存 */
    dma_init_struct.memory0_addr        = (uint32_t)s_dma_rx_buffer;     /* 目的：接收缓冲区 */
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number              = RS485_RX_BUFFER_SIZE;          /* 一次最多收满整个缓冲区 */
    dma_init_struct.periph_addr         = (uint32_t)&USART_DATA(USART1); /* 源：USART1_DATA */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;

    dma_single_data_mode_init(DMA0, RS485_DMA_RX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(DMA0, RS485_DMA_RX_CHANNEL, DMA_SUBPERI4);

    /* 接收通道：配置完立即启动，DMA 在后台持续监听 USART1 数据 */
    dma_channel_enable(DMA0, RS485_DMA_RX_CHANNEL);
}

/* ============================================================
 * 初始化
 * ============================================================ */

void rs485_dma_init(uint32_t baudrate)
{
    /* --- 第 1 步：开外设时钟（比 IDLE 模式多一个 DMA0 时钟） --- */
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_USART1);
    rcu_periph_clock_enable(RCU_DMA0);

    /* --- 第 2 步：PD5/PD6 配为 USART1 复用 (AF7) --- */
    gpio_af_set(RS485_GPIO_PORT, GPIO_AF_7, RS485_TX_PIN | RS485_RX_PIN);
    gpio_mode_set(RS485_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  RS485_TX_PIN | RS485_RX_PIN);
    gpio_output_options_set(RS485_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            RS485_TX_PIN | RS485_RX_PIN);

    /* --- 第 3 步：DE 引脚推挽输出，默认低 = 接收 --- */
    gpio_mode_set(RS485_DE_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RS485_DE_PIN);
    gpio_output_options_set(RS485_DE_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, RS485_DE_PIN);
    gpio_bit_reset(RS485_DE_PORT, RS485_DE_PIN);

    /* --- 第 4 步：USART1 通信参数 (8N1) --- */
    usart_deinit(RS485_USART);
    usart_word_length_set(RS485_USART, USART_WL_8BIT);
    usart_stop_bit_set(RS485_USART, USART_STB_1BIT);
    usart_parity_config(RS485_USART, USART_PM_NONE);
    usart_baudrate_set(RS485_USART, baudrate);
    usart_transmit_config(RS485_USART, USART_TRANSMIT_ENABLE);
    usart_receive_config(RS485_USART, USART_RECEIVE_ENABLE);

    /* --- 第 5 步：初始化 DMA 通道 --- */
    rs485_dma_tx_channel_init();   /* 发送通道：仅配置，不启动 */
    rs485_dma_rx_channel_init();   /* 接收通道：配置 + 立即启动 */

    /* --- 第 6 步：把 USART1 的发送/接收信号源接到 DMA ---
     * 这一步让 USART1 不再在每个字节产生 RBNE/TBE 中断，
     * 而是直接向 DMA 控制器发请求。 */
    usart_dma_transmit_config(RS485_USART, USART_TRANSMIT_DMA_ENABLE);
    usart_dma_receive_config(RS485_USART, USART_RECEIVE_DMA_ENABLE);

    /* --- 第 7 步：NVIC + IDLE 中断 ---
     * DMA 模式下不需要 RBNE 中断，接收完全交给 DMA。
     * 只需要 IDLE 中断来检测"一帧结束"。 */
    nvic_irq_enable(USART1_IRQn, 3U, 2U);
    usart_interrupt_enable(RS485_USART, USART_INT_IDLE);

    /* --- 第 8 步：使能 USART1 --- */
    usart_enable(RS485_USART);
}

/* ============================================================
 * 发送 —— DMA + DE 时序
 * ============================================================ */

/**
 * 发送时序详解（与 IDLE 模式相同的 DE 控制规则）：
 *   ① 拷贝用户数据到 DMA 发送缓冲区
 *   ② DE 拉高 → 切到驱动模式
 *   ③ 重配 DMA 通道：关→清标志→设地址/长度→开
 *   ④ 等 DMA FTF：DMA 把数据全部搬到 USART_DATA 寄存器
 *   ⑤ **等 USART_FLAG_TC**：移位寄存器把最后一字节真正发出去
 *      —— FTF 早于 TC 大约 1~2 个字节时长（取决于波特率）。
 *         这一步是 RS485 DMA 模式与 USART0 DMA 模式的关键差别，绝对不能省。
 *   ⑥ DE 拉低 → 释放总线
 */
void rs485_dma_send_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t index;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    /* 超过发送缓冲区容量则截断 */
    if (len > RS485_FRAME_BUFFER_SIZE)
    {
        len = RS485_FRAME_BUFFER_SIZE;
    }

    /* 等上一次 DMA 发送完成（防止重入） */
    while (s_dma_tx_busy != 0U)
    {
    }
    s_dma_tx_busy = 1U;

    /* ① 把用户数据复制到 DMA 发送缓冲区
     *    DMA 期间用户的 data 可能被改写或位于 Flash 上不便直接搬运，
     *    统一中转到 s_dma_tx_buffer 既安全又对齐统一。 */
    for (index = 0U; index < len; index++)
    {
        s_dma_tx_buffer[index] = data[index];
    }

    /* ② DE 拉高 —— 必须在 DMA 启动之前拉高，否则首字节出去时收发器还在接收模式 */
    gpio_bit_set(RS485_DE_PORT, RS485_DE_PIN);

    /* ③ 重配 DMA 发送通道（关 → 清旧标志 → 设地址/数量 → 开） */
    dma_channel_disable(DMA0, RS485_DMA_TX_CHANNEL);
    dma_flag_clear(DMA0, RS485_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    dma_memory_address_config(DMA0, RS485_DMA_TX_CHANNEL,
                              DMA_MEMORY_0, (uint32_t)s_dma_tx_buffer);
    dma_transfer_number_config(DMA0, RS485_DMA_TX_CHANNEL, len);

    /*
     * TC 是 USART 的“上一帧已经完全移出”的粘性状态。
     * 如果不在启动本次 DMA 前清掉它，下面等待 TC 的 while 可能读到上一帧留下的 SET，
     * 从而提前拉低 RS485 DE，截断本次发送的最后 1~2 个字节。
     */
    usart_flag_clear(RS485_USART, USART_FLAG_TC);
    dma_channel_enable(DMA0, RS485_DMA_TX_CHANNEL);

    /* ④ 等 DMA 全部搬运完成 */
    while (dma_flag_get(DMA0, RS485_DMA_TX_CHANNEL, DMA_FLAG_FTF) == RESET)
    {
    }

    /* ⑤ 关键：等 USART TC，确认最后一字节已离开移位寄存器。
     *    省略这一步会截断最后一字节。 */
    while (usart_flag_get(RS485_USART, USART_FLAG_TC) == RESET)
    {
    }

    /* ⑥ DE 拉低，释放总线 */
    gpio_bit_reset(RS485_DE_PORT, RS485_DE_PIN);

    /* 清 DMA 标志，恢复发送忙位 */
    dma_flag_clear(DMA0, RS485_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    s_dma_tx_busy = 0U;
}

/* ============================================================
 * 帧取出
 * ============================================================ */

uint8_t rs485_dma_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
{
    uint16_t copy_len;
    uint16_t index;

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

    return 1U;
}

uint8_t rs485_dma_is_busy(void)
{
    return s_dma_tx_busy;
}

/* ============================================================
 * USART1 中断服务函数
 *
 * DMA 模式下接收交给 DMA，CPU 唯一要处理的就是 IDLE 事件：
 *   暂停 DMA → 算长度 → 搬数据到帧缓冲区 → 重启 DMA。
 * ============================================================ */

void USART1_IRQHandler(void)
{
    uint16_t received_length;
    uint16_t index;

    if (usart_interrupt_flag_get(RS485_USART, USART_INT_FLAG_IDLE) != RESET)
    {
        /* 读 STAT0 + DATA 清除 IDLE 标志 */
        (void)USART_STAT0(RS485_USART);
        (void)USART_DATA(RS485_USART);

        /* 暂停 DMA 接收：搬运数据期间必须关闭，否则新到字节会污染数据 */
        dma_channel_disable(DMA0, RS485_DMA_RX_CHANNEL);

        /* 已接收数量 = 总容量 - DMA 剩余传输数。
         * 例：配 256 字节，DMA 剩 233 → 已收 23 字节。 */
        received_length = (uint16_t)(RS485_RX_BUFFER_SIZE -
                          dma_transfer_number_get(DMA0, RS485_DMA_RX_CHANNEL));

        if (received_length > RS485_FRAME_BUFFER_SIZE)
        {
            received_length = RS485_FRAME_BUFFER_SIZE;
        }

        /* 搬运数据：DMA 接收缓冲区 → 帧缓冲区 */
        for (index = 0U; index < received_length; index++)
        {
            s_frame_buffer[index] = s_dma_rx_buffer[index];
        }

        s_frame_length = received_length;
        s_frame_ready  = (received_length != 0U) ? 1U : 0U;

        /* 重启 DMA 接收：地址回到缓冲区头 + 数量恢复满容量 */
        dma_memory_address_config(DMA0, RS485_DMA_RX_CHANNEL,
                                  DMA_MEMORY_0, (uint32_t)s_dma_rx_buffer);
        dma_transfer_number_config(DMA0, RS485_DMA_RX_CHANNEL, RS485_RX_BUFFER_SIZE);
        dma_flag_clear(DMA0, RS485_DMA_RX_CHANNEL, DMA_FLAG_FTF);
        dma_channel_enable(DMA0, RS485_DMA_RX_CHANNEL);
    }
}

#endif /* RS485_RX_MODE == RS485_MODE_DMA */
