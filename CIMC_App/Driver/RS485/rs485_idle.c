/************************************************************
 * 文件：rs485_idle.c
 * 说明：GD32 RS485 (USART1) —— 中断接收 + IDLE 空闲检测分帧
 *
 * 与 usart_idle.c 的关系：
 *   接收逻辑结构完全一致（RBNE 逐字节入缓冲，IDLE 拼帧），
 *   差别只有两点：
 *     1. 外设由 USART0 换成 USART1（中断号、向量名都不同）
 *     2. 发送时必须用 DE 引脚切换收发器方向，且 DE 落沿要等 TC 完成后才能拉低
 *
 * 编译守卫：
 *   整个文件被 #if (RS485_RX_MODE == RS485_MODE_IDLE) 包裹，
 *   只有用户在 RS485.h 里选了 IDLE 模式时才会参与编译。
 ************************************************************/

#include "rs485_idle.h"

#if (RS485_RX_MODE == RS485_MODE_IDLE)

/* ============================================================
 * 静态变量 —— 与中断共享，必须 volatile
 * ============================================================ */

/** RBNE 中断里逐字节追加的"半成品帧"缓冲区 */
static volatile uint8_t  s_irq_rx_buffer[RS485_RX_BUFFER_SIZE];

/** 当前已接收字节数 = s_irq_rx_buffer 的写指针 */
static volatile uint16_t s_irq_rx_length = 0U;

/** IDLE 触发后从 s_irq_rx_buffer 搬过来的完整帧 */
static volatile uint8_t  s_frame_buffer[RS485_FRAME_BUFFER_SIZE];

/** 完整帧的实际字节数 */
static volatile uint16_t s_frame_length = 0U;

/** 帧就绪标志：0 = 无帧，1 = 有新帧等待主循环取走 */
static volatile uint8_t  s_frame_ready = 0U;

/** 接收溢出标志：一帧超过缓冲区容量时置位，后续字节被丢弃 */
static volatile uint8_t  s_overflow_flag = 0U;

/* ============================================================
 * 初始化
 * ============================================================ */

void rs485_idle_init(uint32_t baudrate)
{
    /* --- 第 1 步：开外设时钟 ---
     * GPIOD 给 PD5/PD6 用，GPIOE 给 DE(PE8) 用，USART1 给通信外设用。
     * 三条时钟线缺一不可，否则后面对应寄存器写入都不生效。 */
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_USART1);

    /* --- 第 2 步：PD5/PD6 配为 USART1 复用功能 ---
     * GD32F4xx 上 USART1 在 PD5/PD6 上的复用号是 AF7。 */
    gpio_af_set(RS485_GPIO_PORT, GPIO_AF_7, RS485_TX_PIN | RS485_RX_PIN);
    gpio_mode_set(RS485_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  RS485_TX_PIN | RS485_RX_PIN);
    gpio_output_options_set(RS485_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            RS485_TX_PIN | RS485_RX_PIN);

    /* --- 第 3 步：DE 引脚配为推挽输出，默认低 = 接收 ---
     * 上电默认进入"监听"状态，避免初始化期间误占用总线。 */
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

    /* --- 第 5 步：NVIC + 接收相关中断 ---
     * 优先级 (3, 2) 与 USART0 同档：互不抢占，也不会挤压 SysTick。 */
    nvic_irq_enable(USART1_IRQn, 3U, 2U);
    usart_interrupt_enable(RS485_USART, USART_INT_RBNE);
    usart_interrupt_enable(RS485_USART, USART_INT_IDLE);

    /* --- 第 6 步：开启外设 --- */
    usart_enable(RS485_USART);
}

/* ============================================================
 * 发送 —— 阻塞 + DE 时序
 * ============================================================ */

/**
 * 发送时序详解：
 *   ① DE 拉高 → 收发器切换到驱动模式（典型 < 1us 完成）
 *   ② 逐字节写 USART_DATA：写入后等 TBE，意味着数据已搬到移位寄存器
 *   ③ 全部字节写完后，**等 TC 标志**：TC 表示移位寄存器把最后一字节也发完了
 *      —— 这一步不能省。TBE 只代表"数据寄存器空"，最后一字节这时还在移位寄存器
 *         里向 TX 引脚一位一位地输出。如果不等 TC 就拉低 DE，最后一字节会被截断，
 *         对端收到不完整字节，CRC/校验直接失败。
 *   ④ DE 拉低 → 释放总线，回到接收监听
 */
void rs485_idle_send_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t index;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    /* ① DE 拉高，切到驱动模式 */
    gpio_bit_set(RS485_DE_PORT, RS485_DE_PIN);

    /* ② 逐字节写入并轮询 TBE */
    for (index = 0U; index < len; index++)
    {
        usart_data_transmit(RS485_USART, data[index]);
        while (usart_flag_get(RS485_USART, USART_FLAG_TBE) == RESET)
        {
        }
    }

    /* ③ 关键：等 TC，确保最后一字节真的被移出去 */
    while (usart_flag_get(RS485_USART, USART_FLAG_TC) == RESET)
    {
    }

    /* ④ DE 拉低，释放总线 */
    gpio_bit_reset(RS485_DE_PORT, RS485_DE_PIN);
}

/* ============================================================
 * 帧取出
 * ============================================================ */

uint8_t rs485_idle_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
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

    /* 用户缓冲区不够大就只复制能装下的部分，剩下的直接丢弃 */
    copy_len = s_frame_length;
    if (copy_len > max_len)
    {
        copy_len = max_len;
    }

    /* 源是 volatile，逐字节复制更直观；memcpy 也能用，但需要类型转换 */
    for (index = 0U; index < copy_len; index++)
    {
        out_buf[index] = s_frame_buffer[index];
    }

    *out_len       = copy_len;
    s_frame_length = 0U;
    s_frame_ready  = 0U;

    return 1U;
}

uint8_t rs485_idle_get_overflow_flag(void)
{
    return s_overflow_flag;
}

void rs485_idle_clear_overflow_flag(void)
{
    s_overflow_flag = 0U;
}

/* ============================================================
 * USART1 中断服务函数
 *
 * 函数名 USART1_IRQHandler 是 CMSIS 启动文件中预留的固定符号，
 * 不能改名也不能加 static。硬件触发 USART1 中断后会自动跳到这里。
 *
 * 同一次进中断可能 RBNE 和 IDLE 都置位（最后一字节到达的同时总线刚好空闲），
 * 所以两个分支用独立的 if，不要写成 else if。
 * ============================================================ */

void USART1_IRQHandler(void)
{
    uint16_t received_data;
    uint16_t index;

    /* ---- 处理 RBNE：每收到 1 字节进一次，把字节追加到 IRQ 缓冲区 ---- */
    if (usart_interrupt_flag_get(RS485_USART, USART_INT_FLAG_RBNE) != RESET)
    {
        /* 读 USART_DATA 同时也清除 RBNE 标志（硬件行为） */
        received_data = usart_data_receive(RS485_USART);

        if (s_irq_rx_length < RS485_RX_BUFFER_SIZE)
        {
            s_irq_rx_buffer[s_irq_rx_length++] = (uint8_t)received_data;
        }
        else
        {
            /* 缓冲区满 → 丢弃当前字节并打标记，主循环可查询并告警。
               中断里只打标记，不做 printf 等耗时操作，否则会丢失后续中断。 */
            s_overflow_flag = 1U;
        }
    }

    /* ---- 处理 IDLE：总线空闲触发，把 IRQ 缓冲区的内容当作一帧 ---- */
    if (usart_interrupt_flag_get(RS485_USART, USART_INT_FLAG_IDLE) != RESET)
    {
        /* GD32 手册要求：依次读 STAT0、DATA 才能清除 IDLE 标志 */
        (void)USART_STAT0(RS485_USART);
        (void)USART_DATA(RS485_USART);

        /* 总线噪声有可能在没收到任何数据时触发 IDLE，所以要判一下长度 */
        if (s_irq_rx_length != 0U)
        {
            s_frame_length = s_irq_rx_length;

            if (s_frame_length > RS485_FRAME_BUFFER_SIZE)
            {
                s_frame_length  = RS485_FRAME_BUFFER_SIZE;
                s_overflow_flag = 1U;
            }

            /* 把数据搬到独立的帧缓冲区，让 IRQ 缓冲区立即可以接收下一帧 */
            for (index = 0U; index < s_frame_length; index++)
            {
                s_frame_buffer[index] = s_irq_rx_buffer[index];
            }

            s_irq_rx_length = 0U;
            s_frame_ready   = 1U;
        }
    }
}

#endif /* RS485_RX_MODE == RS485_MODE_IDLE */
