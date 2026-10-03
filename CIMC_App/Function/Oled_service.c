#include "Oled_service.h"
#include "OLED_IIC.h"
#include "AutoReport_service.h"

#define OLED_SERVICE_FONT_SIZE       16U
#define OLED_SERVICE_CHARS_PER_LINE  16U

#define OLED_SERVICE_LINE0_Y         0U
#define OLED_SERVICE_LINE1_Y         2U

#define OLED_SERVICE_TEAM_LINE       "2026397013"
#define OLED_SERVICE_IDLE_LINE       "IDLE"
#define OLED_SERVICE_AUTO_LINE       "AutoSample"

/**
 * @brief 在 OLED 指定页显示一行评分状态文本。
 *
 * @param y OLED 起始页号。16 像素高字体占用两个页，因此两行分别从 0、2 页开始。
 * @param text 要显示的业务状态文本，长度超过 16 个 ASCII 字符时会被截断。
 *
 * @details 参考 `19_OLED_driver` 的 `OLED_ShowString(0, 0/16, ..., 16)` 用法。
 *          当前工程的 `OLED_ShowStr()` 使用页坐标，所以用 page 0 和 page 2
 *          显示两行 16px 文本，正好占满 128x32 屏幕。每行补齐到 16 个
 *          字符，避免短文本覆盖长文本后残留旧字符。
 */
static void Oled_Service_ShowPaddedLine(uint8_t y, const char *text)
{
    char buffer[OLED_SERVICE_CHARS_PER_LINE + 1U];
    uint8_t index = 0U;

    while ((index < OLED_SERVICE_CHARS_PER_LINE) && (text[index] != '\0'))
    {
        buffer[index] = text[index];
        index++;
    }

    while (index < OLED_SERVICE_CHARS_PER_LINE)
    {
        buffer[index] = ' ';
        index++;
    }

    buffer[OLED_SERVICE_CHARS_PER_LINE] = '\0';

    OLED_ShowStr(0U, y, buffer, OLED_SERVICE_FONT_SIZE);
}

void Oled_Service_Init(void)
{
    OLED_Init();
    Oled_Service_Task();
}

void Oled_Service_Task(void)
{
    Oled_Service_ShowPaddedLine(OLED_SERVICE_LINE0_Y, OLED_SERVICE_TEAM_LINE);

    if (AutoReport_Service_IsRunning() != 0U)
    {
        Oled_Service_ShowPaddedLine(OLED_SERVICE_LINE1_Y, OLED_SERVICE_AUTO_LINE);
    }
    else
    {
        Oled_Service_ShowPaddedLine(OLED_SERVICE_LINE1_Y, OLED_SERVICE_IDLE_LINE);
    }
}
