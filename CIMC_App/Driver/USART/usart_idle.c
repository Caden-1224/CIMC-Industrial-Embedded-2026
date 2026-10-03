/************************************************************
 * 文件：usart_idle.c
 * 说明：GD32 USART0 —— 中断接收 + IDLE 空闲检测分帧
 *
 * 工作原理（整体数据流）：
 *
 *   发送方向（简单）：
 *     用户调用 → usart_idle_send_bytes() → 逐字节写 USART_DATA 寄存器
 *     → while 轮询 TBE 标志等发送缓冲区空 → 最后一字节后轮询 TC 等
 *     发送完成 → 返回
 *
 *   接收方向（核心）：
 *     串口硬件每收到 1 字节 → 触发 RBNE（读数据缓冲区非空）中断
 *     → USART0_IRQHandler 把字节存入 s_irq_rx_buffer[]
 *     → 当总线空闲一个字节时间 → 触发 IDLE 中断
 *     → USART0_IRQHandler 把 s_irq_rx_buffer 的数据批量复制到
 *       s_frame_buffer，置位 s_frame_ready = 1
 *     → 主循环中调用 usart_idle_fetch_frame() 检测到 s_frame_ready
 *     → 把帧数据复制到用户提供的 out_buf，清零 s_frame_ready
 *
 *   "分帧"的含义：
 *     串口数据是连续的字节流，没有天然的"包边界"。
 *     IDLE 检测利用了这样一个事实：上位机发完一帧数据后，
 *     总线上会有一小段空闲时间（没有任何电平跳变）。
 *     USART 硬件检测到这个空闲后置 IDLE 标志，
 *     我们就认为"一帧结束了"，把缓冲区里的数据当做一个完整帧。
 *
 * 编译守卫：
 *   整个文件被 #if (USART0_RX_MODE == USART0_MODE_IDLE) 包裹，
 *   只有用户在 USART.h 里选了 IDLE 模式时才会参与编译。
 *   否则这个 .c 文件编译出来的就是空的目标文件，不占 Flash。
 ************************************************************/

#include "usart_idle.h"

#if (USART0_RX_MODE == USART0_MODE_IDLE)

/* ============================================================
 * 静态变量 —— 全部用 static 限制文件作用域
 *
 * volatile 关键字说明：
 *   这些变量在 中断（USART0_IRQHandler）和 主循环
 *   两个执行上下文中被共享访问。编译器优化时可能会把变量
 *   缓存到寄存器里，导致中断里修改了值但主循环看不到。
 *   volatile 强制每次读写都直接访问内存，保证两个上下文
 *   看到一致的值。
 * ============================================================ */

/**
 * s_irq_rx_buffer[] — IRQ 里的逐字节接收缓冲区
 *
 * RBNE 中断每触发一次，就把新字节追加到这个数组里。
 * 它是"正在拼接中的半成品帧"。
 * 当 IDLE 中断触发时，说明帧结束了，这时候 s_irq_rx_buffer
 * 里的内容就是一帧完整数据。
 */
static volatile uint8_t  s_irq_rx_buffer[USART0_RX_BUFFER_SIZE];

/**
 * s_irq_rx_length — 当前已接收的字节数（在 s_irq_rx_buffer 中的写入位置）
 *
 * 每次 RBNE 中断 +1，IDLE 中断处理完后清零。
 * 它是 s_irq_rx_buffer 的"写指针"。
 */
static volatile uint16_t s_irq_rx_length = 0U;

/**
 * s_frame_buffer[] — 完整帧缓冲区
 *
 * IDLE 中断触发后，把 s_irq_rx_buffer 的数据复制到这里，
 * 主循环从这里取走数据。这起到了"中断 ↔ 主循环"之间的
 * 数据传递作用。
 */
static volatile uint8_t  s_frame_buffer[USART0_FRAME_BUFFER_SIZE];

/**
 * s_frame_length — 当前帧的实际字节长度
 */
static volatile uint16_t s_frame_length = 0U;

/**
 * s_frame_ready — 帧就绪标志
 *
 *   0 = 没有新帧（主循环调用 fetch_frame 会返回 0）
 *   1 = 有一帧新数据等着被取走
 * IDLE 中断里置 1，fetch_frame 取完后清 0。
 */
static volatile uint8_t  s_frame_ready  = 0U;

/**
 * s_overflow_flag — 接收溢出标志
 *
 *   当一帧数据量超过缓冲区容量时置 1，后续字节被丢弃。
 *   主循环可以通过 get_overflow_flag() 检测到这个事件，
 *   然后做告警或重连等处理。
 */
static volatile uint8_t  s_overflow_flag = 0U;

/* ============================================================
 * 初始化函数
 * ============================================================ */

/**
 * @brief  初始化 USART0 外设，配置 GPIO、USART 参数、NVIC 中断。
 *
 * 初始化顺序（每一步都不能跳过或调换）：
 *   1. 开时钟 → 外设没有时钟就不工作
 *   2. 配 GPIO 引脚复用 → 把 PA9/PA10 物理引脚连接到 USART0 内部
 *   3. 配 USART 通信参数 → 波特率、数据位、停止位、校验位
 *   4. 配 NVIC → 让 CPU 能响应 USART0 的中断请求
 *   5. 开中断 → 使能 RBNE（逐字节）和 IDLE（帧结束）两种中断
 *   6. 使能 USART → 最后一步打开外设总开关
 *
 * @param  baudrate: 串口波特率，如 115200U。
 */
void usart_idle_init(uint32_t baudrate)
{
    /* --- 第 1 步：开启外设时钟 ---
     * GPIO 和 USART 是两条独立时钟线，都要开。
     * RCU（Reset and Clock Unit）是 GD32 的时钟控制模块。 */
    rcu_periph_clock_enable(USART0_GPIO_RCU);      /* GPIOA 时钟 */
    rcu_periph_clock_enable(USART0_USART_RCU);     /* USART0 时钟 */

    /* --- 第 2 步：配置 GPIO 引脚为复用功能 ---
     * gpio_af_set:       把引脚映射到 USART0 的 AF（Alternate Function）功能
     * gpio_mode_set:     设置引脚模式为 AF（复用），无上下拉电阻
     * gpio_output_options_set: 推挽输出，50MHz 速率 */
    gpio_af_set(USART0_PORT, USART0_GPIO_AF, USART0_TX_PIN | USART0_RX_PIN);
    gpio_mode_set(USART0_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  USART0_TX_PIN | USART0_RX_PIN);
    gpio_output_options_set(USART0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            USART0_TX_PIN | USART0_RX_PIN);

    /* --- 第 3 步：配置 USART 通信参数 ---
     * usart_deinit:       先复位外设到出厂默认状态，防止上次配置残留
     * USART_WL_8BIT:      数据位 = 8 位（最常用的 8N1 格式）
     * USART_STB_1BIT:     停止位 = 1 位
     * USART_PM_NONE:      无校验位
     * baudrate:           波特率由用户传入
     * TRANSMIT_ENABLE:    使能发送器
     * RECEIVE_ENABLE:     使能接收器 */
    usart_deinit(USART0_PERIPH);
    usart_word_length_set(USART0_PERIPH, USART_WL_8BIT);
    usart_stop_bit_set(USART0_PERIPH, USART_STB_1BIT);
    usart_parity_config(USART0_PERIPH, USART_PM_NONE);
    usart_baudrate_set(USART0_PERIPH, baudrate);
    usart_transmit_config(USART0_PERIPH, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART0_PERIPH, USART_RECEIVE_ENABLE);

    /* --- 第 4 步：配置 NVIC（嵌套向量中断控制器） ---
     * 参数 3U = 抢占优先级（0 最高，数字越大优先级越低）
     * 参数 2U = 子优先级
     * 串口中断优先级不宜太高，否则会挤压 SysTick 等系统心跳。 */
    nvic_irq_enable(USART0_IRQn, 3U, 2U);

    /* --- 第 5 步：开启两种接收相关中断 ---
     * RBNE（Read Buffer Not Empty）：每收到 1 字节触发一次
     * IDLE：总线空闲一个字符时间后触发一次（用于分帧） */
    usart_interrupt_enable(USART0_PERIPH, USART_INT_RBNE);
    usart_interrupt_enable(USART0_PERIPH, USART_INT_IDLE);

    /* --- 第 6 步：使能 USART 外设 --- */
    usart_enable(USART0_PERIPH);
}

/* ============================================================
 * 发送函数
 * ============================================================ */

/**
 * @brief  通过串口发送一段原始字节流（阻塞式）。
 *
 * "阻塞式"的含义：函数内部用 while 死循环等待硬件标志位，
 * CPU 一直卡在这里，直到所有字节发完才返回。
 * 优点：简单可靠。缺点：发送期间 CPU 不能做其他事。
 *
 * 发送过程（以发送 3 字节 0xAA 0xBB 0xCC 为例）：
 *   第 1 轮循环：写 0xAA → 等 TBE → 写 0xBB → 等 TBE → 写 0xCC → 等 TBE
 *   全部写入后：等 TC（Transmission Complete，移位寄存器发完最后一字节）
 *   TC 置位 → 返回
 *
 * TBE（Transmit Buffer Empty）：发送数据寄存器空，可以写下一个字节了
 * TC  (Transmission Complete)：移位寄存器也发完了，线路上真正空闲了
 *
 * @param  data: 指向待发送数据首字节的指针。
 * @param  len:  待发送字节数。
 */
void usart_idle_send_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t index = 0U;

    /* 防御性检查：空指针或零长度直接返回 */
    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    /* 逐字节发送：写一个字节 → 等 TBE → 写下一个 */
    for (index = 0U; index < len; index++)
    {
        /* 把当前字节写入 USART 数据寄存器，硬件会自动开始发送 */
        usart_data_transmit(USART0_PERIPH, data[index]);

        /* 等待 TBE=1：发送缓冲区已空，可以写下一个字节了。
         * 注意：TBE=1 不代表当前字节已经从 TX 引脚发出去了，
         * 只代表数据已经从"数据寄存器"搬到了"移位寄存器"，
         * 数据寄存器腾出来了，可以接着写。 */
        while (usart_flag_get(USART0_PERIPH, USART_FLAG_TBE) == RESET)
        {
        }
    }

    /* 等待 TC=1：移位寄存器把最后一个字节也发完了。
     * 这一步保证函数返回时，TX 线路上确实已经空闲，
     * 后续代码（如切换引脚方向）不会干扰正在发送的数据。 */
    while (usart_flag_get(USART0_PERIPH, USART_FLAG_TC) == RESET)
    {
    }
}

/* ============================================================
 * 帧读取函数
 * ============================================================ */

/**
 * @brief  从帧缓冲区中取出一帧完整数据。
 *
 * 这个函数设计为在主循环中高频轮询调用。
 * 调用时序：
 *   主循环每次迭代调用一次 → 检查 s_frame_ready
 *   → 如果为 0，说明没有新帧，立即返回 0
 *   → 如果为 1，把 s_frame_buffer 的数据复制到 out_buf，
 *     清除 s_frame_ready，返回 1
 *
 * 为什么要"复制"而不是"直接给指针"：
 *   因为中断随时可能再次触发并覆写 s_frame_buffer，
 *   如果直接暴露指针，用户读到一半数据就被中断改了，
 *   那就会拿到半新半旧的脏数据。复制一份是安全的做法。
 *
 * @param  out_buf: 用户提供的缓冲区，帧数据将被复制到这里。
 * @param  out_len: 输出参数，返回帧的实际字节数。
 * @param  max_len: out_buf 的最大容量，防止复制越界。
 * @return 1 = 成功取到一帧，0 = 当前没有完整帧。
 */
uint8_t usart_idle_fetch_frame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
{
    uint16_t copy_len = 0U;
    uint16_t index    = 0U;

    /* 参数合法性检查 */
    if ((out_buf == NULL) || (out_len == NULL))
    {
        return 0U;
    }

    /* 没有新帧 → 直接返回 0，调用者下次再来 */
    if (s_frame_ready == 0U)
    {
        return 0U;
    }

    /* 决定实际复制长度：取"帧长度"和"用户缓冲区容量"的较小值 */
    copy_len = s_frame_length;
    if (copy_len > max_len)
    {
        copy_len = max_len;    /* 用户缓冲区不够大，只复制能装下的部分 */
    }

    /* 逐字节复制（不使用 memcpy，因为源是 volatile） */
    for (index = 0U; index < copy_len; index++)
    {
        out_buf[index] = s_frame_buffer[index];
    }

    /* 把实际复制长度告诉用户 */
    *out_len = copy_len;

    /* 清零帧状态，为接收下一帧做准备 */
    s_frame_length = 0U;
    s_frame_ready  = 0U;

    return 1U;
}

/**
 * @brief  查询是否发生过接收溢出。
 * @return 1 = 溢出过，0 = 未溢出。
 */
uint8_t usart_idle_get_overflow_flag(void)
{
    return s_overflow_flag;
}

/**
 * @brief  清除溢出标志（用户处理完溢出事件后调用）。
 */
void usart_idle_clear_overflow_flag(void)
{
    s_overflow_flag = 0U;
}

/* ============================================================
 * 中断服务函数 —— 本文件的核心
 *
 * 函数名 USART0_IRQHandler 是 CMSIS 启动文件中硬编码的名字，
 * 不能改，不能加 static。启动文件的中断向量表里存的就是
 * 这个名字的函数地址，硬件触发中断后会自动跳转到这里。
 *
 * 这个函数里处理两种中断：
 *   1. RBNE — 每收到 1 字节触发，把字节存入缓冲区
 *   2. IDLE — 总线空闲触发，把缓冲区内容当作一帧
 *
 * 两个 if 是独立的，不是 else if。
 * 因为同一次进中断可能同时有 RBNE 和 IDLE（最后一字节到达的同时
 * 总线也空闲了），两个都要处理。
 * ============================================================ */

/**
 * @brief  USART0 中断服务函数。
 *
 * RBNE 处理流程：
 *   硬件收到字节 → RBNE 标志置 1 → 进这里
 *   → 读 USART_DATA 寄存器取走字节（读操作同时清除 RBNE 标志）
 *   → 存入 s_irq_rx_buffer[s_irq_rx_length++]
 *   → 如果缓冲区满了就置溢出标志，丢弃后续字节
 *
 * IDLE 处理流程：
 *   总线空闲一个字符时间 → IDLE 标志置 1 → 进这里
 *   → 读 USART_DATA 寄存器（手册要求：必须读一次才能清除 IDLE 标志）
 *   → 把 s_irq_rx_buffer 的数据搬到 s_frame_buffer
 *   → 置 s_frame_ready = 1，通知主循环有新帧
 *   → 清零 s_irq_rx_length，准备接收下一帧
 */
void USART0_IRQHandler(void)
{
    uint16_t received_data = 0U;
    uint16_t index         = 0U;

    /* ---- 处理 RBNE 中断：逐字节接收 ---- */
    if (usart_interrupt_flag_get(USART0_PERIPH, USART_INT_FLAG_RBNE) != RESET)
    {
        /*
         * 读取 USART_DATA 寄存器：
         *   1. 取出接收到的字节
         *   2. 读操作会自动清除 RBNE 标志（硬件行为，不需要手动清）
         */
        received_data = usart_data_receive(USART0_PERIPH);

        if (s_irq_rx_length < USART0_RX_BUFFER_SIZE)
        {
            /* 缓冲区还有空位 → 存入字节，写指针后移 */
            s_irq_rx_buffer[s_irq_rx_length++] = (uint8_t)received_data;
        }
        else
        {
            /*
             * 缓冲区已满 → 置溢出标志，丢弃当前字节
             * 注意：这里只打标记，不做任何耗时操作（如 printf），
             * 因为在中断里做耗时操作会导致后续中断丢失。
             */
            s_overflow_flag = 1U;
        }
    }

    /* ---- 处理 IDLE 中断：帧结束分帧 ---- */
    if (usart_interrupt_flag_get(USART0_PERIPH, USART_INT_FLAG_IDLE) != RESET)
    {
        /*
         * 必须读一次 USART_DATA 寄存器才能清除 IDLE 标志
         * （GD32 参考手册明确要求：读 USART_DATA 或读 USART_STAT0 清除）
         * 返回值没有用，强制转换为 void 丢弃，同时消除编译器警告。
         */
        (void)USART_STAT0(USART0_PERIPH);
        (void)USART_DATA(USART0_PERIPH);

        /* 只有确实收到了数据才需要分帧（防止总线噪声误触发） */
        if (s_irq_rx_length != 0U)
        {
            /* 记录本帧长度 */
            s_frame_length = s_irq_rx_length;

            /* 如果帧长超过了帧缓冲区容量，截断并标记溢出 */
            if (s_frame_length > USART0_FRAME_BUFFER_SIZE)
            {
                s_frame_length  = USART0_FRAME_BUFFER_SIZE;
                s_overflow_flag = 1U;
            }

            /*
             * 把 IRQ 缓冲区的数据搬到帧缓冲区
             * 为什么不直接让主循环读 s_irq_rx_buffer？
             *   因为搬运完后立即清零 s_irq_rx_length，
             *   下一帧的 RBNE 中断马上就会开始往 s_irq_rx_buffer 写新数据，
             *   如果不搬到独立缓冲区，主循环可能读到新帧和旧帧混在一起的数据。
             */
            for (index = 0U; index < s_frame_length; index++)
            {
                s_frame_buffer[index] = s_irq_rx_buffer[index];
            }

            /* 清零 IRQ 写指针，为下一帧接收做准备 */
            s_irq_rx_length = 0U;

            /* 置位帧就绪标志，通知主循环"有新帧可以取了" */
            s_frame_ready = 1U;
        }
    }
}

#endif /* USART0_RX_MODE == USART0_MODE_IDLE */
