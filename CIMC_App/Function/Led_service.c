#include "Led_service.h"

#define LED_SERVICE_SYSTEM_LED_INDEX       1U
#define LED_SERVICE_HEARTBEAT_INTERVAL_MS  1000UL

/**
 * @brief LED 引脚映射表，索引与 led_state[] 一一对应。
 */
static const uint32_t led_pin_map[LED_NUM] = {
    LED1_PIN,
    LED2_PIN,
    LED3_PIN,
    LED4_PIN,
    LED5_PIN,
    LED6_PIN};

uint8_t led_state[LED_NUM] = {0};

/**
 * @brief 按 1s 周期翻转 APP 系统状态 LED。
 *
 * @details SysTick ISR 只维护毫秒计数；LED 翻转放在主循环轮询任务中完成，
 *          避免在中断上下文访问 GPIO 或引入额外定时器。LED1 已保留给
 *          自动上报采集状态，因此这里使用 LED2 作为 APP 运行心跳。
 */
static void Led_Service_UpdateHeartbeat(void)
{
    static uint32_t last_toggle_ms = 0UL;
    uint32_t now_ms;

    if (LED_SERVICE_SYSTEM_LED_INDEX >= LED_NUM)
    {
        return;
    }

    now_ms = get_millis();
    if ((uint32_t)(now_ms - last_toggle_ms) >= LED_SERVICE_HEARTBEAT_INTERVAL_MS)
    {
        led_state[LED_SERVICE_SYSTEM_LED_INDEX] = (uint8_t)(!led_state[LED_SERVICE_SYSTEM_LED_INDEX]);
        last_toggle_ms = now_ms;
    }
}

/**
 * @brief 初始化 LED 服务。
 *
 * @details 调用底层 GPIO 初始化，并把所有 LED 默认拉低熄灭。随后周期任务会接管
 *          LED2 心跳，自动上报服务会按业务状态接管 LED1。
 */
void Led_Service_Init(void)
{
    uint8_t i;

    LED_Init();

    for (i = 0U; i < LED_NUM; i++)
    {
        led_state[i] = 0U;
        gpio_bit_reset(LED_PORT, led_pin_map[i]);
    }
}

/**
 * @brief 周期刷新 LED 输出状态。
 *
 * @details 第一步先更新 APP 心跳位；第二步再把 led_state[] 的每一位映射到
 *          实际 GPIO。这样业务服务只需要改 led_state[]，不会直接依赖 GPIO。
 */
void Led_Service_Task(void)
{
    uint8_t i;

    Led_Service_UpdateHeartbeat();

    for (i = 0U; i < LED_NUM; i++)
    {
        if (led_state[i] != 0U)
        {
            gpio_bit_set(LED_PORT, led_pin_map[i]);
        }
        else
        {
            gpio_bit_reset(LED_PORT, led_pin_map[i]);
        }
    }
}
