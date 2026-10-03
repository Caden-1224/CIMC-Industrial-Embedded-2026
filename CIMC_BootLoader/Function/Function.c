#include "Function.h"
#include "bl_core.h"
#include "usart.h"
#include "bl_oled.h"

void System_Init(void)
{
    systick_config();
    my_usart_init(bootloader_get_upgrade_baudrate());
    BlOled_Init();
    BlOled_ShowBootloader();
}

void UsrFunction(void)
{
    bootloader_run();

    while (1)
    {
    }
}
