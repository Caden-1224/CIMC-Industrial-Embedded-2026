#include "Key_service.h"
#include "KEY.h"
#include "Led_service.h"

/**
 * @brief  按固定表格循环切换 PWM 占空比，用于 LED 亮度验证。
 * @param  void 无输入参数。
 * @return void 无返回值。
 */

uint16_t pwm_duty_table[] = {0, 10, 30, 50, 70, 90, 100};
size_t pwm_duty_index = 0;

void Key_Service_Init(void)
{
    KEY_Init();
}

void Key_Service_Task(void)
{
    if (KEY_Stat(KEY_PORT, KEY1_PIN))
    {
        pwm_duty_index = (pwm_duty_index + 1) % (sizeof(pwm_duty_table) / sizeof(pwm_duty_table[0]));
        my_timer_set_duty(pwm_duty_table[pwm_duty_index]);
    }
    if (KEY_Stat(KEY_PORT, KEY2_PIN))
        led_state[1] = !led_state[1];
    if (KEY_Stat(KEY_PORT, KEY3_PIN))
        led_state[2] = !led_state[2];
    if (KEY_Stat(KEY_PORT, KEY4_PIN))
        led_state[3] = !led_state[3];
    if (KEY_Stat(KEY_PORT, KEY5_PIN))
        led_state[4] = !led_state[4];
    if (KEY_Stat(KEY_PORT, KEY6_PIN))
        led_state[5] = !led_state[5];
}
