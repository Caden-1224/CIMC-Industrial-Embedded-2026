#include "Usart_service.h"
#include "USART.h"
#include "Rtc_service.h"

/** 帧接收缓冲区，FetchFrame 的输出目�?*/
static uint8_t  s_frame_buf[USART0_FRAME_BUFFER_SIZE];

/** 当前帧长�?*/
static uint16_t s_frame_len = 0U;

/**
 * @brief  就地规范�?USART0 命令帧�? * @param  frame  USART0_FetchFrame() 返回的帧缓冲区�? * @param  frame_len 规范化前后的帧长度（输入/输出参数）�? * @param  capacity  帧的总可写容量�? * @return void
 *
 * USART 驱动基于字节流，因此获取的帧不保证是 C 字符串�? * 串口终端通常还会附加 CR/LF。此辅助函数�?strcmp() 使用�? * 确保命令匹配的确定性�? */
static void Usart_Service_NormalizeFrame(uint8_t *frame, uint16_t *frame_len, uint16_t capacity)
{
    uint16_t normalized_len;

    if ((frame == NULL) || (frame_len == NULL) || (capacity == 0U))
    {
        return;
    }

    normalized_len = *frame_len;

    /* 即使接收帧恰好填满目标缓冲区，也要为字符串终止符预留 1 个字节�?*/
    if (normalized_len >= capacity)
    {
        normalized_len = (uint16_t)(capacity - 1U);
    }

    /* 去除 PC 串口工具在回车后通常附加的终止符�?       在服务层处理可以保持底层 USART 驱动只负责原始帧的职责�?*/
    while ((normalized_len > 0U) &&
           ((frame[normalized_len - 1U] == '\r') ||
            (frame[normalized_len - 1U] == '\n') ||
            (frame[normalized_len - 1U] == ' ')  ||
            (frame[normalized_len - 1U] == '\t')))
    {
        normalized_len--;
    }

    frame[normalized_len] = '\0';
    *frame_len = normalized_len;
}


/**
 * @brief  初始�?USART0，波特率 115200�? *         打印一�?Ready，用来验�?TX 方向是否正常�? */
void Usart_Service_Init(void)
{
    USART0_Init(115200U);
    USART0_Printf("USART0 Ready!\r\n");
}

void Usart_Service_Task(void)
{
    if (USART0_FetchFrame(s_frame_buf, &s_frame_len, USART0_FRAME_BUFFER_SIZE))
    {
        if (BootUpdate_Service_HandleFrame(s_frame_buf, s_frame_len) != 0U)
        {
            return;
        }

        Usart_Service_NormalizeFrame(s_frame_buf, &s_frame_len, USART0_FRAME_BUFFER_SIZE);

        if (s_frame_len == 0U)
        {
            return;
        }

        if(strcmp((const char *)s_frame_buf, "time") == 0)
        {
            /* 显示当前 RTC 时间 */
            RTC_Service_ShowTime();
        }

        else
        {
            USART0_Printf("ERR unknown command: %s\r\n", (const char *)s_frame_buf);
        }
    }
}

/*!
    \brief      �?USART0 接收两位 ASCII 数字字符，校验并返回十进制数�?    \param[in]  value  输入值的十进制上限（如年�?99，月�?12，小�?23�?    \param[out] none
    \retval     成功返回十进制�?(0~value)，失败返�?0xFF（输入非数字字符或超出上限）

    \details
    执行步骤（按顺序）：

    [步骤1] 字符接收与格式校�?—�?循环接收 2 个字�?    - while (RESET == usart_flag_get(...))：阻塞等�?RBNE（接收缓冲非空）标志
      原因：USART 硬件收到一个字节后置位 RBNE，软件轮询该位确认数据就�?    - usart_data_receive(USART0)：从数据寄存�?USART_DATA 读出 1 字节
    - ASCII 范围校验：tmp[i] 必须�?0x30('0') ~ 0x39('9') 之间
      若不在范围内 �?打印提示，index-- 回退重新接收该位

    [步骤2] ASCII �?十进制数值转�?    - ASCII �?'0' = 0x30�?1' = 0x31�?..�?9' = 0x39
    - 减去 0x30 后得到真实数字�?0~9
    - 十位数字(tmp[0])×10 + 个位数字(tmp[1]) �?得到完整十进制数�?      例如：输入字�?"25"
        tmp[0]='2'(0x32) �?0x32-0x30=2, tmp[1]='5'(0x35) �?0x35-0x30=5
        index = 5 + (2×10) = 25

    [步骤3] 范围校验
    - 若十进制数�?> value（入参上限）�?提示非法，返�?0xFF
    - 调用方以 while (xxx == 0xFF) 循环等待合法输入

    @note   不做 BCD 转换，直接返回十进制数值。调用方按需自行编码�?*/
uint8_t Usart_InputDecimal(uint32_t value)
{
    uint32_t index = 0;
    uint32_t tmp[2] = {0, 0};

    /* === 步骤1：逐个接收 2 �?ASCII 字符并校验合法�?=== */
    while (index < 2)
    {
        /* 阻塞等待 USART0 接收缓冲非空（RBNE = Receive Buffer Not Empty�?         * 轮询方式：适用于非中断场景，无 RTOS 任务调度时不占用 CPU */
        while (RESET == usart_flag_get(USART0, USART_FLAG_RBNE))
            ;
        /* �?USART_DATA 寄存器读取接收到�?1 字节 */
        tmp[index++] = usart_data_receive(USART0);

        /* ASCII 范围校验：[0x30, 0x39] �?'0' ~ '9'
         * 若接收到的字符不在数字字符范围内，则判定为非法输�?*/
        if ((tmp[index - 1] < 0x30) || (tmp[index - 1] > 0x39))
        {
            USART0_Printf("\n\r please input a valid number between 0 and 9 \n\r");
            /* index-- 回退：丢弃非法字符，等待用户重新输入该位 */
            index--;
        }
    }

    /* === 步骤2：ASCII �?十进制数�?=== */
    /* tmp[1] 是个位，tmp[0] 是十�?     * 减去 0x30 �?ASCII 码转为真实数�?0~9
     * 十位×10 + 个位 = 完整十进制数 */
    index = (tmp[1] - 0x30) + ((tmp[0] - 0x30) * 10);

    /* === 步骤3：范围校�?=== */
    /* 若用户输入的数值超过入参上限，提示并返�?0xFF */
    if (index > value)
    {
        USART0_Printf("\n\r please input a valid number between 0 and %d \n\r", value);
        return 0xFF;
    }

    return (uint8_t)index;
}
