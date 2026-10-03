#include "LED.h"

void LED_Init(void)
{
    rcu_periph_clock_enable(RCU_GPIOE); // 使能 GPIOE 时钟

    gpio_mode_set(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);
    // 设置 LED 引脚为输出模式，无上下拉

    gpio_output_options_set(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);

    gpio_bit_reset(LED_PORT,
                   LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN | LED5_PIN | LED6_PIN);
}
