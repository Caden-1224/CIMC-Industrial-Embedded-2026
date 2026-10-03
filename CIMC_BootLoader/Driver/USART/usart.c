/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：usart.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2025/12/30     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "usart.h"

/************************* 宏定义 *************************/
#define BL_USART_DE_SETTLE_DELAY 120U
#define BL_USART_RX_BUFFER_SIZE 512U
#define BL_USART_RX_RING_SIZE 8192U


/************************ 变量定义 ************************/
 //! 接收数据的缓冲区
uint8_t recv_buf[BL_USART_RX_BUFFER_SIZE] = { 0 };
uint16_t recv_len = 0;
//! 接收数据的数组
uint8_t recv_real_buf[BL_USART_RX_BUFFER_SIZE] = { 0 };
uint16_t recv_real_len = 0;
//! 接收数据的标记位
uint8_t recv_flag = 0;

static uint8_t s_rx_ring[BL_USART_RX_RING_SIZE] = { 0 };
static volatile uint16_t s_rx_ring_head = 0U;
static volatile uint16_t s_rx_ring_tail = 0U;


/************************ 函数定义 ************************/



/************************************************************
 * Function :       usart_init
 * Comment  :       用于初始化USART0
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void my_usart_init(uint32_t baudrate)
{
	//! 首先开启对应的中断处理函数     <------------------------------
	nvic_irq_enable(USART_IRQn , 3 , 2);

	//! 使能USART时钟
	rcu_periph_clock_enable(USART_RCU);
	rcu_periph_clock_enable(USART_PIN_RCU);
	rcu_periph_clock_enable(RS485_DE_RCU);

	gpio_mode_set(RS485_DE_PORT , GPIO_MODE_OUTPUT , GPIO_PUPD_NONE , RS485_DE_PIN);
	gpio_output_options_set(RS485_DE_PORT , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , RS485_DE_PIN);
	gpio_bit_reset(RS485_DE_PORT , RS485_DE_PIN);

	// 对TX配置成为复用模式
	gpio_af_set(USART_PORT , GPIO_AF_7 , USART_RX_Pin);

	// 设置GPIO引脚模式
	gpio_mode_set(USART_PORT , GPIO_MODE_AF , GPIO_PUPD_NONE , USART_RX_Pin);
	// 设置GPIO引脚参数
	gpio_output_options_set(USART_PORT , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , USART_RX_Pin);

	// 对TX配置成为复用模式
	gpio_af_set(USART_PORT , GPIO_AF_7 , USART_TX_Pin);

	// 设置GPIO引脚模式
	gpio_mode_set(USART_PORT , GPIO_MODE_AF , GPIO_PUPD_NONE , USART_TX_Pin);
	// 设置GPIO引脚参数
	gpio_output_options_set(USART_PORT , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , USART_TX_Pin);

	// 配置USART
	usart_deinit(USART);
	usart_baudrate_set(USART , baudrate);
	usart_receive_config(USART , USART_RECEIVE_ENABLE);
	usart_transmit_config(USART , USART_TRANSMIT_ENABLE);
	usart_enable(USART);

	//! 启动串口中断
	usart_interrupt_enable(USART , USART_INT_RBNE);
	usart_interrupt_enable(USART , USART_INT_IDLE);

}
static uint16_t bl_usart_rx_ring_next(uint16_t index)
{
    index++;
    if (index >= BL_USART_RX_RING_SIZE)
    {
        index = 0U;
    }
    return index;
}

static void bl_usart_rx_ring_push(uint8_t byte)
{
    uint16_t next_head = bl_usart_rx_ring_next(s_rx_ring_head);

    /*
     * N-series official OTA sends a 6420-byte raw bin as a continuous stream.
     * The legacy IDLE packet buffer is only 512 bytes, so the interrupt path
     * mirrors every fresh byte into this BootLoader-owned ring. If the host keeps
     * writing faster than the foreground can validate, discard the oldest byte so
     * the newest command/bin prefix remains recoverable for the retry window.
     */
    if (next_head == s_rx_ring_tail)
    {
        s_rx_ring_tail = bl_usart_rx_ring_next(s_rx_ring_tail);
    }

    s_rx_ring[s_rx_ring_head] = byte;
    s_rx_ring_head = next_head;
}

bool bl_usart_read_byte(uint8_t *byte)
{
    bool has_byte = false;

    if (byte == NULL)
    {
        return false;
    }

    __disable_irq();
    if (s_rx_ring_tail != s_rx_ring_head)
    {
        *byte = s_rx_ring[s_rx_ring_tail];
        s_rx_ring_tail = bl_usart_rx_ring_next(s_rx_ring_tail);
        has_byte = true;
    }
    __enable_irq();

    return has_byte;
}

void bl_usart_clear_rx_stream(void)
{
    __disable_irq();
    s_rx_ring_head = 0U;
    s_rx_ring_tail = 0U;
    recv_len = 0U;
    recv_real_len = 0U;
    recv_flag = 0U;
    __enable_irq();
}

/************************************************************
 * Function :       usart_recv_buf
 * Comment  :       用于处理USART0接收缓冲区数据
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void usart_recv_buf(void)
{
	if (recv_flag)
	{
		printf("recv_real_buf: %s\n" , recv_real_buf);
		recv_flag = 0;
	}
}

/************************************************************
 * Function :       fputc
 * Comment  :       用于处理USART0发送数据
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
int fputc(int ch , FILE* f)
{
	volatile uint16_t de_delay;

	gpio_bit_set(RS485_DE_PORT , RS485_DE_PIN);
	for (de_delay = 0U; de_delay < BL_USART_DE_SETTLE_DELAY; de_delay++)
	{
	}
	usart_flag_clear(USART , USART_FLAG_TC);
	while (RESET == usart_flag_get(USART , USART_FLAG_TBE));
	usart_data_transmit(USART , (uint8_t)ch);
	while (RESET == usart_flag_get(USART , USART_FLAG_TC));
	gpio_bit_reset(RS485_DE_PORT , RS485_DE_PIN);
	return ch;
}

/************************************************************
 * Function :       USART0_IRQHandler
 * Comment  :       用于处理USART0中断
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2025-12-30 V0.1 original
************************************************************/
void USART1_IRQHandler(void)
{
    if (usart_interrupt_flag_get(USART , USART_INT_FLAG_RBNE) != RESET)
    {
        uint8_t rx_byte = (uint8_t)usart_data_receive(USART);

        bl_usart_rx_ring_push(rx_byte);
        if (recv_len < BL_USART_RX_BUFFER_SIZE)
        {
            recv_buf[recv_len++] = rx_byte;
        }
    }
    //! 进行不定长数据的接收
    if (usart_interrupt_flag_get(USART , USART_INT_FLAG_IDLE) != RESET && recv_len != 0)
    {
        usart_data_receive(USART);
        memcpy(recv_real_buf , recv_buf , recv_len);
        recv_real_len = recv_len;
        recv_len = 0;
        recv_flag = 1;
        //! 清除IDLE中断标志位
        // usart_interrupt_flag_clear(USART, USART_INT_FLAG_IDLE);
    }
}

/****************************End*****************************/
