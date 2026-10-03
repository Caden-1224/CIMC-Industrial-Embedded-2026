#include "KEY.h"

/**
 * 边沿检测的本质：
 *   只关心"按键从松开变成按下"这一瞬间，松开过程和按住保持都不算事件。
 *   每次轮询保存当前电平到 s_prev_state，下一轮拿当前电平和它比较：
 *   只有"上一次=高、这一次=低"才算一次按下事件。
 *
 * 为什么不再需要 delay_1ms(20) 软件去抖：
 *   Key_Service_Task 本身就是 20ms 周期调度。机械按键的抖动通常 < 10ms，
 *   两次相邻轮询之间有 20ms 间隔，已经天然完成了去抖采样。
 *   旧版本在 KEY_Stat 内部 delay_1ms(20) + while(等抬键) 是一种
 *   "把所有阻塞都塞进检测函数"的旧式写法，与时间片调度框架直接冲突。
 *
 * 为什么要做 pin → slot 映射：
 *   上层 Key_Service_Task 用 KEY_Stat(KEY_PORT, KEY1_PIN) 这种引脚常量调用，
 *   而边沿检测必须按"哪个键"独立保存上次状态。
 *   表驱动把 GPIO_PIN_xx 映射到 0..5 的内部槽位，状态就能按键分别记录。
 */

/** 按键引脚到内部状态槽位的映射表，与 s_prev_state 一一对应 */
static const uint16_t s_key_pin_table[6] = {
    KEY1_PIN, KEY2_PIN, KEY3_PIN,
    KEY4_PIN, KEY5_PIN, KEY6_PIN
};

/**
 * s_prev_state[i] 保存第 i 个键上一次轮询时的电平。
 * 初值 1（高）= 假设上电时全部松开，避免上电瞬间被误判成按下事件。
 */
static uint8_t s_prev_state[6] = {1U, 1U, 1U, 1U, 1U, 1U};

void KEY_Init(void)
{
    rcu_periph_clock_enable(RCU_GPIOD);
    gpio_mode_set(KEY_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE,
                  KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN | KEY5_PIN | KEY6_PIN);
}

/**
 * @brief  按键按下沿检测（非阻塞）。
 * @param  port  按键所在 GPIO 端口（与 KEY_PORT 一致）。
 * @param  pin   按键引脚（KEYx_PIN）。
 * @return 1 = 本次轮询检测到"高 → 低"按下沿；0 = 没按 / 还在按住 / 引脚非法。
 *
 * 调用契约：
 *   必须以固定周期反复调用（典型 20ms / 次），每次只看相邻两次轮询之间的电平变化。
 *   按住不动只会在第一次返回 1，之后一直返回 0；松开时不会触发事件。
 *   函数全程无 delay 无 while，最差情况是几次寄存器读 + 比较，绝不会卡住调度器。
 */
uint8_t KEY_Stat(uint32_t port, uint16_t pin)
{
    uint8_t slot;
    uint8_t curr_level;
    uint8_t event = 0U;

    /* 在映射表里找出该引脚对应的状态槽位，找不到就直接退出（防御性） */
    for (slot = 0U; slot < 6U; slot++)
    {
        if (s_key_pin_table[slot] == pin)
        {
            break;
        }
    }
    if (slot >= 6U)
    {
        return 0U;
    }

    /* 读当前电平：低电平 = 按下（按键接 GND，松开靠外部上拉拉高） */
    curr_level = (gpio_input_bit_get(port, pin) == RESET) ? 0U : 1U;

    /* 边沿判定：上次高、这次低 → 按下事件 */
    if ((s_prev_state[slot] == 1U) && (curr_level == 0U))
    {
        event = 1U;
    }

    /* 无论是否触发事件，都更新上次电平，供下一轮比较 */
    s_prev_state[slot] = curr_level;

    return event;
}
