#include "bl_oled.h"
#include "gd32f4xx.h"
#include "systick.h"
#include <stddef.h>

#define BL_OLED_ADDR       0x78U
#define BL_OLED_CMD        0U
#define BL_OLED_DATA       1U
#define BL_OLED_WIDTH      128U
#define BL_OLED_PAGE_COUNT 4U
#define BL_OLED_SCL_PORT   GPIOB
#define BL_OLED_SCL_PIN    GPIO_PIN_8
#define BL_OLED_SDA_PORT   GPIOB
#define BL_OLED_SDA_PIN    GPIO_PIN_9

static const uint8_t s_font8x16[][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0xE0, 0x10, 0x08, 0x08, 0x10, 0xE0, 0x00,
     0x00, 0x0F, 0x10, 0x20, 0x20, 0x10, 0x0F, 0x00},
    {0x00, 0x10, 0x10, 0xF8, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x20, 0x20, 0x3F, 0x20, 0x20, 0x00, 0x00},
    {0x00, 0x70, 0x08, 0x08, 0x08, 0x88, 0x70, 0x00,
     0x00, 0x30, 0x28, 0x24, 0x22, 0x21, 0x30, 0x00},
    {0x00, 0x30, 0x08, 0x88, 0x88, 0x48, 0x30, 0x00,
     0x00, 0x18, 0x20, 0x20, 0x20, 0x11, 0x0E, 0x00},
    {0x00, 0x00, 0xC0, 0x20, 0x10, 0xF8, 0x00, 0x00,
     0x00, 0x07, 0x04, 0x24, 0x24, 0x3F, 0x24, 0x00},
    {0x00, 0xF8, 0x08, 0x88, 0x88, 0x08, 0x08, 0x00,
     0x00, 0x19, 0x21, 0x20, 0x20, 0x11, 0x0E, 0x00},
    {0x00, 0xE0, 0x10, 0x88, 0x88, 0x18, 0x00, 0x00,
     0x00, 0x0F, 0x11, 0x20, 0x20, 0x11, 0x0E, 0x00},
    {0x00, 0x38, 0x08, 0x08, 0xC8, 0x38, 0x08, 0x00,
     0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x70, 0x88, 0x08, 0x08, 0x88, 0x70, 0x00,
     0x00, 0x1C, 0x22, 0x21, 0x21, 0x22, 0x1C, 0x00},
    {0x00, 0xE0, 0x10, 0x08, 0x08, 0x10, 0xE0, 0x00,
     0x00, 0x00, 0x31, 0x22, 0x22, 0x11, 0x0F, 0x00},
    {0x08, 0xF8, 0x88, 0x88, 0x88, 0x70, 0x00, 0x00,
     0x20, 0x3F, 0x20, 0x20, 0x20, 0x11, 0x0E, 0x00},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
     0x00, 0x19, 0x24, 0x22, 0x22, 0x22, 0x3F, 0x20},
    {0x08, 0xF8, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00,
     0x00, 0x3F, 0x11, 0x20, 0x20, 0x11, 0x0E, 0x00},
    {0x00, 0x00, 0x00, 0x80, 0x80, 0x88, 0xF8, 0x00,
     0x00, 0x0E, 0x11, 0x20, 0x20, 0x10, 0x3F, 0x20},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
     0x00, 0x1F, 0x22, 0x22, 0x22, 0x22, 0x13, 0x00},
    {0x00, 0x08, 0x08, 0xF8, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x20, 0x20, 0x3F, 0x20, 0x20, 0x00, 0x00},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
     0x00, 0x1F, 0x20, 0x20, 0x20, 0x20, 0x1F, 0x00},
    {0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x80, 0x00,
     0x20, 0x20, 0x3F, 0x21, 0x20, 0x00, 0x01, 0x00},
    {0x00, 0x80, 0x80, 0xE0, 0x80, 0x80, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x1F, 0x20, 0x20, 0x00, 0x00}
};

static void bl_oled_scl_set(void)
{
    gpio_bit_set(BL_OLED_SCL_PORT, BL_OLED_SCL_PIN);
}

static void bl_oled_scl_clr(void)
{
    gpio_bit_reset(BL_OLED_SCL_PORT, BL_OLED_SCL_PIN);
}

static void bl_oled_sda_set(void)
{
    gpio_bit_set(BL_OLED_SDA_PORT, BL_OLED_SDA_PIN);
}

static void bl_oled_sda_clr(void)
{
    gpio_bit_reset(BL_OLED_SDA_PORT, BL_OLED_SDA_PIN);
}

static void bl_oled_delay(void)
{
    volatile uint8_t index;

    for (index = 0U; index < 8U; index++)
    {
    }
}

static void bl_oled_start(void)
{
    bl_oled_sda_set();
    bl_oled_scl_set();
    bl_oled_delay();
    bl_oled_sda_clr();
    bl_oled_delay();
    bl_oled_scl_clr();
}

static void bl_oled_stop(void)
{
    bl_oled_scl_clr();
    bl_oled_sda_clr();
    bl_oled_delay();
    bl_oled_scl_set();
    bl_oled_delay();
    bl_oled_sda_set();
    bl_oled_delay();
}

static void bl_oled_write_byte(uint8_t data)
{
    uint8_t bit_index;

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        bl_oled_scl_clr();
        if ((data & 0x80U) != 0U)
        {
            bl_oled_sda_set();
        }
        else
        {
            bl_oled_sda_clr();
        }
        data <<= 1U;
        bl_oled_delay();
        bl_oled_scl_set();
        bl_oled_delay();
    }

    bl_oled_scl_clr();
    bl_oled_sda_set();
    bl_oled_delay();
    bl_oled_scl_set();
    bl_oled_delay();
    bl_oled_scl_clr();
}

static void bl_oled_write(uint8_t data, uint8_t mode)
{
    bl_oled_start();
    bl_oled_write_byte(BL_OLED_ADDR);
    bl_oled_write_byte((mode == BL_OLED_CMD) ? 0x00U : 0x40U);
    bl_oled_write_byte(data);
    bl_oled_stop();
}

static void bl_oled_set_pos(uint8_t x, uint8_t page)
{
    bl_oled_write((uint8_t)(0xB0U + page), BL_OLED_CMD);
    bl_oled_write((uint8_t)(0x00U | (x & 0x0FU)), BL_OLED_CMD);
    bl_oled_write((uint8_t)(0x10U | ((x >> 4U) & 0x0FU)), BL_OLED_CMD);
}

static void bl_oled_clear(void)
{
    uint8_t page;
    uint8_t col;

    for (page = 0U; page < BL_OLED_PAGE_COUNT; page++)
    {
        bl_oled_set_pos(0U, page);
        for (col = 0U; col < BL_OLED_WIDTH; col++)
        {
            bl_oled_write(0x00U, BL_OLED_DATA);
        }
    }
}

static const uint8_t *bl_oled_lookup_font(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return s_font8x16[ch - '0' + 1];
    }

    switch (ch)
    {
        case 'B':
            return s_font8x16[11];
        case 'a':
            return s_font8x16[12];
        case 'b':
            return s_font8x16[13];
        case 'd':
            return s_font8x16[14];
        case 'e':
            return s_font8x16[15];
        case 'l':
            return s_font8x16[16];
        case 'o':
            return s_font8x16[17];
        case 'r':
            return s_font8x16[18];
        case 't':
            return s_font8x16[19];
        default:
            return s_font8x16[0];
    }
}

static void bl_oled_show_char8x16(uint8_t x, uint8_t page, char ch)
{
    const uint8_t *font = bl_oled_lookup_font(ch);
    uint8_t col;

    bl_oled_set_pos(x, page);
    for (col = 0U; col < 8U; col++)
    {
        bl_oled_write(font[col], BL_OLED_DATA);
    }

    bl_oled_set_pos(x, (uint8_t)(page + 1U));
    for (col = 0U; col < 8U; col++)
    {
        bl_oled_write(font[col + 8U], BL_OLED_DATA);
    }
}

static void bl_oled_show_string16(uint8_t x, uint8_t page, const char *text)
{
    while ((text != NULL) && (*text != '\0') && (x <= (BL_OLED_WIDTH - 8U)))
    {
        bl_oled_show_char8x16(x, page, *text);
        x = (uint8_t)(x + 8U);
        text++;
    }
}

void BlOled_Init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_mode_set(BL_OLED_SCL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, BL_OLED_SCL_PIN);
    gpio_output_options_set(BL_OLED_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, BL_OLED_SCL_PIN);
    gpio_mode_set(BL_OLED_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, BL_OLED_SDA_PIN);
    gpio_output_options_set(BL_OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, BL_OLED_SDA_PIN);

    bl_oled_scl_set();
    bl_oled_sda_set();
    delay_1ms(100U);

    bl_oled_write(0xAEU, BL_OLED_CMD);
    bl_oled_write(0x20U, BL_OLED_CMD);
    bl_oled_write(0x02U, BL_OLED_CMD);
    bl_oled_write(0xB0U, BL_OLED_CMD);
    bl_oled_write(0xC8U, BL_OLED_CMD);
    bl_oled_write(0x00U, BL_OLED_CMD);
    bl_oled_write(0x10U, BL_OLED_CMD);
    bl_oled_write(0x40U, BL_OLED_CMD);
    bl_oled_write(0x81U, BL_OLED_CMD);
    bl_oled_write(0x7FU, BL_OLED_CMD);
    bl_oled_write(0xA1U, BL_OLED_CMD);
    bl_oled_write(0xA6U, BL_OLED_CMD);
    bl_oled_write(0xA8U, BL_OLED_CMD);
    bl_oled_write(0x1FU, BL_OLED_CMD);
    bl_oled_write(0xA4U, BL_OLED_CMD);
    bl_oled_write(0xD3U, BL_OLED_CMD);
    bl_oled_write(0x00U, BL_OLED_CMD);
    bl_oled_write(0xD5U, BL_OLED_CMD);
    bl_oled_write(0x80U, BL_OLED_CMD);
    bl_oled_write(0xD9U, BL_OLED_CMD);
    bl_oled_write(0xF1U, BL_OLED_CMD);
    bl_oled_write(0xDAU, BL_OLED_CMD);
    bl_oled_write(0x02U, BL_OLED_CMD);
    bl_oled_write(0xDBU, BL_OLED_CMD);
    bl_oled_write(0x40U, BL_OLED_CMD);
    bl_oled_write(0x8DU, BL_OLED_CMD);
    bl_oled_write(0x14U, BL_OLED_CMD);
    bl_oled_write(0xAFU, BL_OLED_CMD);
    bl_oled_clear();
}

void BlOled_ShowBootloader(void)
{
    bl_oled_clear();
    bl_oled_show_string16(0U, 0U, "2026397013");
    bl_oled_show_string16(0U, 2U, "Bootloader");
}
