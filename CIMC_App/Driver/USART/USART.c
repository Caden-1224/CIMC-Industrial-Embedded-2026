/************************************************************
 * 文件：USART.c
 * 说明：USART 统一驱动 —— 外壳调度层
 *
 * 职责：
 *   1. 根据 USART.h 中用户选择的 USART0_RX_MODE，
 *      在编译期决定引入哪套底层实现（IDLE 或 DMA）。
 *   2. 对外暴露统一的公共 API（USART0_Init / USART0_SendBytes / USART0_Printf ...），
 *      内部把调用原封不动转发给底层驱动。
 *   3. 提供 USART0_Printf 格式化输出（vsnprintf + 一次 SendBytes）。
 *
 * 本文件不包含任何硬件寄存器操作，纯粹是一个"路由器"。
 ************************************************************/

#include "USART.h"
#include <stdarg.h>

/* ============================================================
 * 编译期模式选择 —— 根据宏引入对应的底层头文件
 *
 * 原理：C 预处理器在编译前展开 #if，只有被选中的分支对应的
 *       头文件才会被 #include 进来，另一套根本不参与编译。
 *       这样两套驱动各自定义的 USART0_IRQHandler、静态变量等
 *       绝不会产生链接冲突。
 *
 * USART0_MODE_IDLE → 中断逐字节接收 + IDLE 空闲检测分帧
 * USART0_MODE_DMA  → DMA 硬件搬运接收 + IDLE 空闲检测分帧
 * ============================================================ */
#if (USART0_RX_MODE == USART0_MODE_IDLE)
    /* 选了 IDLE 模式 → 包含中断+IDLE 驱动的函数声明 */
    #include "usart_idle.h"

#elif (USART0_RX_MODE == USART0_MODE_DMA)
    /* 选了 DMA 模式 → 包含 DMA+IDLE 驱动的函数声明 */
    #include "usart_dma.h"

#else
    /* 用户填了非法值，直接让编译器报错停下，避免静默出错 */
    #error "USART0_RX_MODE must be USART0_MODE_IDLE or USART0_MODE_DMA"
#endif

/* ============================================================
 * 统一公共接口 —— 薄转发层
 *
 * 每个函数内部就是一个 #if 选择，把参数原样透传给底层实现。
 * 上层调用者（Function.c、服务层）只需要知道 USART_xxx 这套 API，
 * 完全不需要关心底层用的是中断还是 DMA。
 * ============================================================ */

/**
 * @brief  初始化 USART0。
 *
 * 根据编译期选定的模式，调用对应的底层初始化函数。
 * 内部会完成：GPIO 复用配置、USART 外设参数配置、NVIC 中断配置，
 * IDLE 模式额外开启 RBNE 中断，DMA 模式额外配置 DMA 通道。
 *
 * @param  baudrate: 目标波特率，如 115200U、9600U。
 */
void USART0_Init(uint32_t baudrate)
{
#if (USART0_RX_MODE == USART0_MODE_IDLE)
    usart_idle_init(baudrate);      /* → 中断模式初始化 */
#else
    usart_dma_init(baudrate);       /* → DMA 模式初始化 */
#endif
}

/**
 * @brief  通过串口发送一段原始字节流（阻塞式）。
 *
 * 阻塞式 = 函数会一直等到所有字节从发送移位寄存器出去后才返回。
 * IDLE 模式内部用 while 轮询 TBE/TC 标志位逐字节发；
 * DMA 模式内部启动 DMA 传输并等待 DMA 完成标志。
 *
 * @param  data: 指向待发送数据首字节的指针，不可为 NULL。
 * @param  len:  待发送字节数，不可为 0。
 */
void USART0_SendBytes(const uint8_t *data, uint16_t len)
{
#if (USART0_RX_MODE == USART0_MODE_IDLE)
    usart_idle_send_bytes(data, len);
#else
    usart_dma_send_bytes(data, len);
#endif
}

/**
 * @brief  从帧缓冲区中取出一帧完整的接收数据。
 *
 * 两种接收模式都使用 IDLE（总线空闲）检测来分帧：
 *   当串口总线上连续一个字节时间没有新数据到来时，
 *   硬件触发 IDLE 中断，此时认为"一帧结束"。
 *
 * 本函数在主循环中轮询调用：
 *   - 返回 1 → 成功取到一帧，数据在 out_buf 中，长度在 out_len 中。
 *   - 返回 0 → 当前没有完整帧，下次循环再来。
 *
 * 取帧后内部会自动清除"帧就绪"标志，等待下一帧到来。
 *
 * @param  out_buf: 用户提供的缓冲区，用于存放帧数据。
 * @param  out_len: 输出帧的实际长度（单位：字节）。
 * @param  max_len: out_buf 的最大容量，防止越界。
 * @return 1 = 成功取帧，0 = 无帧可取。
 */
uint8_t USART0_FetchFrame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
{
#if (USART0_RX_MODE == USART0_MODE_IDLE)
    return usart_idle_fetch_frame(out_buf, out_len, max_len);
#else
    return usart_dma_fetch_frame(out_buf, out_len, max_len);
#endif
}

/* ============================================================
 * 模式专属接口 —— 条件编译
 *
 * 这部分 API 只在对应模式下才会被编译出来。
 * IDLE 模式有溢出标志（因为 IRQ 里逐字节入 buffer，满了就溢出）。
 * DMA 模式有发送忙标志（DMA 传输期间不允许重配通道）。
 * ============================================================ */

#if (USART0_RX_MODE == USART0_MODE_IDLE)

/**
 * @brief  查询接收缓冲区是否发生过溢出。
 *
 * IDLE 模式下，中断里逐字节写入 s_irq_rx_buffer，
 * 如果一帧数据超过 USART0_RX_BUFFER_SIZE，后续字节会被丢弃，
 * 同时置位溢出标志。主循环中可以定期检查并做处理（如打印告警）。
 *
 * @return 1 = 曾发生溢出，0 = 未溢出。
 */
uint8_t USART0_GetOverflowFlag(void)
{
    return usart_idle_get_overflow_flag();
}

/**
 * @brief  清除接收溢出标志。
 *
 * 用户在处理完溢出事件（如打印告警、重置状态）后调用，
 * 防止下次循环重复进入溢出处理分支。
 */
void USART0_ClearOverflowFlag(void)
{
    usart_idle_clear_overflow_flag();
}

#endif /* USART0_MODE_IDLE */

#if (USART0_RX_MODE == USART0_MODE_DMA)

/**
 * @brief  查询 DMA 发送通道是否正在忙碌。
 *
 * DMA 模式下发送是异步的：调用 usart_dma_send_bytes 后，
 * DMA 控制器在后台搬运数据。如果用户想在发送完成前做别的事，
 * 可以先检查此标志。当前实现中 send_bytes 内部已经阻塞等待，
 * 所以这个接口主要是给将来"非阻塞发送"预留的。
 *
 * @return 1 = DMA 正在发送，0 = 空闲。
 */
uint8_t USART0_IsBusy(void)
{
    return usart_dma_is_busy();
}

#endif /* USART0_MODE_DMA */

/* ============================================================
 * 格式化串口输出 —— USART0_Printf
 *
 * 为什么不用 fputc 重定向：
 *   fputc 是逐字符回调，printf("hello %d\r\n", 42) 会触发 10+ 次
 *   fputc → 每次 fputc 调用一次 USART0_SendBytes(&ch, 1)。
 *   在 DMA 模式下，每次 SendBytes 都要走一遍完整的
 *   "关通道 → 清标志 → 重配地址/数量 → 开通道 → 等 FTF → 等 TC" 流程，
 *   打印一行短文本就要启停 DMA 十几次，非常浪费。
 *
 *   USART0_Printf 的做法：
 *   先用 vsnprintf 把格式化结果一次性写入内部缓冲区（纯 CPU 操作），
 *   然后只调一次 USART0_SendBytes 把整段数据发出去。
 *   DMA 只需启停 1 次，效率提升显著。
 *
 * 使用方法（与 printf 完全一致）：
 *   USART0_Printf("temp = %d\r\n", temp);
 *   USART0_Printf("recv %d bytes: %s\r\n", len, buf);
 * ============================================================ */

/**
 * @brief  格式化打印到串口。
 *
 * @param  format: 格式化字符串，支持所有标准 printf 占位符（%d %s %x %f ...）。
 * @param  ...:    可变参数。
 * @return 实际发送的字节数（不含 '\0'）。
 */
int USART0_Printf(const char *format, ...)
{
    char buffer[USART0_PRINTF_BUF_SIZE];
    va_list args;
    int len = 0;

    if (format == NULL)
    {
        return 0;
    }

    /* 第 1 步：把 format + 可变参数 格式化为字符串写入 buffer */
    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    /* vsnprintf 返回值可能超过缓冲区容量，截断到缓冲区实际大小 */
    if (len > (int)sizeof(buffer))
    {
        len = (int)sizeof(buffer);
    }

    /* 安全检查：格式化失败（如编码错误）返回负值，直接跳过 */
    if (len <= 0)
    {
        return 0;
    }

    /* 第 2 步：一次性把整段格式化结果通过串口发出去 */
    USART0_SendBytes((const uint8_t *)buffer, (uint16_t)len);

    return len;
}

/**
 * @brief  将 C 标准库 printf() 输出重定向到 USART0。
 * @param  ch  C 标准库格式化器发出的字符。
 * @param  f   标准流句柄，在本裸机重定向中未使用。
 * @return int 已发送的字符，符合 fputc 契约。
 *
 * 仍有旧模块直接调用 printf()。若不提供此重定向，Keil 的 C 标准库
 * 可能会将 stdout 路由到半主机（semihosting）而非调试串口，
 * 导致固件在首次直接调用 USART0_Printf() 后看似停止输出。
 */
int fputc(int ch, FILE *f)
{
    uint8_t data = (uint8_t)ch;

    (void)f;
    USART0_SendBytes(&data, 1U);

    return ch;
}
