/************************************************************
 * 文件：RS485.c
 * 说明：RS485 统一驱动 —— 外壳调度层
 *
 * 职责：
 *   1. 根据 RS485.h 中的 RS485_RX_MODE，编译期决定引入哪套底层实现
 *      (rs485_idle 或 rs485_dma)。
 *   2. 对外暴露统一的公共 API（RS485_Init / RS485_SendBytes / RS485_Printf ...），
 *      内部把调用透传给底层驱动。
 *   3. 提供 RS485_Printf 格式化输出 (vsnprintf + 一次 SendBytes)，
 *      使整段数据在一次 DE 高电平期间发出，避免逐字符切换方向。
 *
 * 本文件不直接操作硬件寄存器，仅做编译期路由 + 文本格式化。
 ************************************************************/

#include "RS485.h"
#include <stdarg.h>

/* ============================================================
 * 编译期模式选择 —— 根据宏引入对应的底层头文件
 *
 * 与 USART.c 的写法一致：被 #if 选中的分支对应的头才会进入编译，
 * 另一套底层 .c 文件在自身的 #if 守卫下编译为空目标，不占 Flash，
 * 也不会出现 USART1_IRQHandler 重复定义的链接冲突。
 * ============================================================ */
#if (RS485_RX_MODE == RS485_MODE_IDLE)
    #include "rs485_idle.h"
#elif (RS485_RX_MODE == RS485_MODE_DMA)
    #include "rs485_dma.h"
#else
    #error "RS485_RX_MODE must be RS485_MODE_IDLE or RS485_MODE_DMA"
#endif

/* ============================================================
 * 统一公共接口 —— 薄转发层
 * ============================================================ */

void RS485_Init(uint32_t baudrate)
{
#if (RS485_RX_MODE == RS485_MODE_IDLE)
    rs485_idle_init(baudrate);
#else
    rs485_dma_init(baudrate);
#endif
}

void RS485_SendBytes(const uint8_t *data, uint16_t len)
{
#if (RS485_RX_MODE == RS485_MODE_IDLE)
    rs485_idle_send_bytes(data, len);
#else
    rs485_dma_send_bytes(data, len);
#endif
}

uint8_t RS485_FetchFrame(uint8_t *out_buf, uint16_t *out_len, uint16_t max_len)
{
#if (RS485_RX_MODE == RS485_MODE_IDLE)
    return rs485_idle_fetch_frame(out_buf, out_len, max_len);
#else
    return rs485_dma_fetch_frame(out_buf, out_len, max_len);
#endif
}

/* =============== 模式专属接口 =============== */

#if (RS485_RX_MODE == RS485_MODE_IDLE)

uint8_t RS485_GetOverflowFlag(void)
{
    return rs485_idle_get_overflow_flag();
}

void RS485_ClearOverflowFlag(void)
{
    rs485_idle_clear_overflow_flag();
}

#endif

#if (RS485_RX_MODE == RS485_MODE_DMA)

uint8_t RS485_IsBusy(void)
{
    return rs485_dma_is_busy();
}

#endif

/* ============================================================
 * 格式化串口输出 —— RS485_Printf
 *
 * 为什么不做 fputc 重定向：
 *   fputc 是逐字符回调，printf("hello %d", 42) 会触发 8+ 次 fputc，
 *   每次 fputc 都会调一次 RS485_SendBytes —— 也就是说每个字符都要
 *   走一遍 "DE↑ → 发 → 等TC → DE↓" 的完整时序，效率极差，
 *   而且每个字符之间的 DE 切换会让对端看到一连串 1 字节短帧。
 *
 *   正确做法是先用 vsnprintf 把整段格式化结果写入栈缓冲区（纯 CPU 操作），
 *   再调一次 RS485_SendBytes 把整段数据在一次 DE 高电平期间发出去。
 * ============================================================ */

int RS485_Printf(const char *format, ...)
{
    /* 256 字节足够日常调试输出。需要更长可在这里临时调大或拆分多次调用。
     * 不抽宏定义是因为本文件只此一处用，宏化反而增加阅读跳转成本。 */
    char buffer[256];
    va_list args;
    int len;

    if (format == NULL)
    {
        return 0;
    }

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    /* vsnprintf 返回的是"如果缓冲足够大本来要写多少字节"，
     * 实际写入量受缓冲区限制，要先截断再发。 */
    if (len > (int)sizeof(buffer))
    {
        len = (int)sizeof(buffer);
    }
    if (len <= 0)
    {
        return 0;
    }

    RS485_SendBytes((const uint8_t *)buffer, (uint16_t)len);
    return len;
}
