#include "Function.h"

typedef struct
{
    void (*task_func)(void);
    uint32_t period_ms;
    uint32_t last_ms;
} slice_poll_t;

static slice_poll_t poll_tasks[] = {
    {Led_Service_Task, 10U, 0U},
    {Key_Service_Task, 20U, 0U},
    {Usart_Service_Task, 10U, 0U},
    {BootUpdate_Service_Task, 20U, 0U},
    {Adc_Service_Task, 50U, 0U},
    {Gd30ad3344_Service_Task, 50U, 0U},
    {AutoReport_Service_Task, 20U, 0U},
    {Alert_Service_Task, 200U, 0U},
    {Sleep_Service_Task, 10U, 0U},
    {Oled_Service_Task, 50U, 0U},
    {RTC_Service_Task, 1000U, 0U},
    {Rs485_Service_Task, 10U, 0U},
};

static const size_t TASK_COUNT = sizeof(poll_tasks) / sizeof(poll_tasks[0]);

void System_Init(void)
{
    __disable_irq();
    SCB->VTOR = BL_APP1_START_ADDR;
    __DSB();
    __ISB();

    systick_config();
    __enable_irq();
    Led_Service_Init();
    Key_Service_Init();
    Usart_Service_Init();
    BootUpdate_Service_Init();
    AppParam_Service_Init();

    Adc_Service_Init();

    Dac_Service_Init();

    Gd30ad3344_Service_Init();

    AutoReport_Service_Init();

    Alert_Service_Init();

    Sleep_Service_Init();

    Oled_Service_Init();

    SpiFlash_Service_Init();

    RTC_Service_Init();

    Rs485_Service_Init();

    Timer_Basic_Init();

    my_timer_init();

}

void System_Loop(void)
{
    uint32_t current_ms = get_millis();
    size_t task_index;

    for (task_index = 0U; task_index < TASK_COUNT; task_index++)
    {
        if ((current_ms - poll_tasks[task_index].last_ms) >= poll_tasks[task_index].period_ms)
        {
            poll_tasks[task_index].task_func();
            poll_tasks[task_index].last_ms = current_ms;
        }
    }
}

void UsrFunction(void)
{
    while (1)
    {
        System_Loop();
    }
}
