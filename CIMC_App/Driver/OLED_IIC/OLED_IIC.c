#include "OLED_IIC.h"
#include "oledfont.h"

u8 OLED_GRAM[144][4]; //128*32分辨�?


/************************************************************
 * Function :       OLED_ColorTurn
 * Comment  :       判断正显还是反显
 * Parameter:       null
 * Return   :       null
 * Author   :
 * Date     :
************************************************************/
void OLED_ColorTurn(u8 i)
{
	if (i == 0)
	{
		OLED_WR_Byte(0xA6, OLED_CMD);//正常显示
	}
	if (i == 1)
	{
		OLED_WR_Byte(0xA7, OLED_CMD);//反色显示
	}
}


/************************************************************
 * Function :       OLED_DisplayTurn
 * Comment  :       屏幕翻转180
 * Parameter:       null
 * Return   :       null
 * Author   :
 * Date     :
************************************************************/
void OLED_DisplayTurn(u8 i)
{
	if (i == 0)
	{
		OLED_WR_Byte(0xC8, OLED_CMD);//正常显示
		OLED_WR_Byte(0xA1, OLED_CMD);
	}
	if (i == 1)
	{
		OLED_WR_Byte(0xC0, OLED_CMD);//反转显示
		OLED_WR_Byte(0xA0, OLED_CMD);
	}
}


/************************************************************
 * Function :       IIC_delay
 * Comment  :       IIC延时
 * Parameter:       null
 * Return   :       null
 * Author   :       Lingyu Meng
 * Date     :
************************************************************/
void IIC_delay(void)
{
	uint16_t t = 120;
	while (t--);
	//delay_1ms();   //测试延时时长
}

/************************************************************
 * Function :       I2C_Start
 * Comment  :       IIC起始信号
 * Parameter:       null
 * Return   :       null
 * Author   :
 * Date     :
************************************************************/
void I2C_Start(void)
{

	// 将SDA重新配置为输�?
	gpio_mode_set(OLED_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SDA_PIN);
	gpio_output_options_set(OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SDA_PIN);

	//软件模拟IIC 操作
	OLED_SDIN_Set();
	OLED_SCLK_Set();
	IIC_delay();
	OLED_SDIN_Clr();
	IIC_delay();
	OLED_SCLK_Clr();
	IIC_delay();
}

/************************************************************
 * Function :       I2C_Stop
 * Comment  :       IIC结束信号
 * Parameter:       null
 * Return   :       null
 * Author   :
 * Date     :
************************************************************/
void I2C_Stop(void)
{

	// 将SDA重新配置为输�?
	gpio_mode_set(OLED_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SDA_PIN);
	gpio_output_options_set(OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SDA_PIN);

	OLED_SDIN_Clr();
	OLED_SCLK_Set();
	IIC_delay();
	OLED_SDIN_Set();
}

//等待信号响应
uint8_t I2C_WaitAck(void) //测数据信号的电平
{
	u8 t = 0U;

	OLED_SDIN_Set();
	IIC_delay();
	OLED_SCLK_Set();
	IIC_delay();
	// 将SDA配置为输�?
	gpio_mode_set(OLED_SDA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, OLED_SDA_PIN);
	while (IIC_READ_SDA)
	{
		t++;
		//printf("%d times:i2c wait out time!\r\n",t);  //测试超时
		if (t > 254)
		{
			//printf("=========================iic TIMEOUT STOP!=====================\r\n"); //超时发送IIC停止信号
			I2C_Stop();
			return 0U;
		}
	}
	OLED_SCLK_Clr();

	IIC_delay();

	// 将SDA重新配置为输�?
	gpio_mode_set(OLED_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SDA_PIN);
	gpio_output_options_set(OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SDA_PIN);

	return 1U;
}

//写入一个字�?
void Send_Byte(u8 dat)
{
	u8 i;
	for (i = 0;i < 8;i++)
	{
		OLED_SCLK_Clr();//将时钟信号设置为低电�?
		if (dat & 0x80)//将dat�?位从最高位依次写入
		{
			OLED_SDIN_Set();
		}
		else
		{
			OLED_SDIN_Clr();
		}
		IIC_delay();
		OLED_SCLK_Set();
		IIC_delay();
		OLED_SCLK_Clr();
		dat <<= 1;
	}
}

//发送一个字�?
//向SSD1306写入一个字节�?
//mode:数据/命令标志 0,表示命令;1,表示数据;
void OLED_WR_Byte(u8 dat, u8 mode)
{
	I2C_Start();
	Send_Byte(0x78);
	if (I2C_WaitAck() == 0U)
	{
		return;
	}
	if (mode) { Send_Byte(0x40); }
	else { Send_Byte(0x00); }
	if (I2C_WaitAck() == 0U)
	{
		return;
	}
	Send_Byte(dat);
	if (I2C_WaitAck() == 0U)
	{
		return;
	}
	I2C_Stop();
}


//开启OLED显示 
void OLED_DisPlay_On(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);//电荷泵使�?
	OLED_WR_Byte(0x14, OLED_CMD);//开启电荷泵
	OLED_WR_Byte(0xAF, OLED_CMD);//点亮屏幕
}

//关闭OLED显示 
void OLED_DisPlay_Off(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);//电荷泵使�?
	OLED_WR_Byte(0x10, OLED_CMD);//关闭电荷�?
	OLED_WR_Byte(0xAE, OLED_CMD);//关闭屏幕
}

//更新显存到OLED	
void OLED_Refresh(void)
{
	u8 i, n;
	for (i = 0;i < 4;i++)
	{
		OLED_WR_Byte(0xb0 + i, OLED_CMD); //设置行起始地址
		OLED_WR_Byte(0x00, OLED_CMD);   //设置低列起始地址
		OLED_WR_Byte(0x10, OLED_CMD);   //设置高列起始地址
		for (n = 0;n < 128;n++)
			OLED_WR_Byte(OLED_GRAM[n][i], OLED_DATA);
	}
}
//清屏函数
void OLED_Clear(void)
{
	u8 i, n;
	for (i = 0;i < 4;i++)
	{
		for (n = 0;n < 128;n++)
		{
			OLED_GRAM[n][i] = 0;//清除所有数�?
		}
	}
	OLED_Refresh();//更新显示
}

//画点 
//x:0~127
//y:0~31
void OLED_DrawPoint(u8 x, u8 y)
{
	u8 i, m, n;
	i = y / 8;
	m = y % 8;
	n = 1 << m;
	OLED_GRAM[x][i] |= n;
}

//清除一个点
//x:0~127
//y:0~31
void OLED_ClearPoint(u8 x, u8 y)
{
	u8 i, m, n;
	i = y / 8;
	m = y % 8;
	n = 1 << m;
	OLED_GRAM[x][i] = ~OLED_GRAM[x][i];
	OLED_GRAM[x][i] |= n;
	OLED_GRAM[x][i] = ~OLED_GRAM[x][i];
}

//画线
//x:0~128
//y:0~31
void OLED_DrawLine(u8 x1, u8 y1, u8 x2, u8 y2)
{
	u8 i, k, k1, k2;
	if (x1 == x2)    //画竖�?
	{
		for (i = 0;i < (y2 - y1);i++)
		{
			OLED_DrawPoint(x1, y1 + i);
		}
	}
	else if (y1 == y2)   //画横�?
	{
		for (i = 0;i < (x2 - x1);i++)
		{
			OLED_DrawPoint(x1 + i, y1);
		}
	}
	else      //画斜�?
	{
		k1 = y2 - y1;
		k2 = x2 - x1;
		k = k1 * 10 / k2;
		for (i = 0;i < (x2 - x1);i++)
		{
			OLED_DrawPoint(x1 + i, y1 + i * k / 10);
		}
	}
}
//x,y:圆心坐标
//r:圆的半径
void OLED_DrawCircle(u8 x, u8 y, u8 r)
{
	int a, b, num;
	a = 0;
	b = r;
	while (2 * b * b >= r * r)
	{
		OLED_DrawPoint(x + a, y - b);
		OLED_DrawPoint(x - a, y - b);
		OLED_DrawPoint(x - a, y + b);
		OLED_DrawPoint(x + a, y + b);

		OLED_DrawPoint(x + b, y + a);
		OLED_DrawPoint(x + b, y - a);
		OLED_DrawPoint(x - b, y - a);
		OLED_DrawPoint(x - b, y + a);

		a++;
		num = (a * a + b * b) - r * r;//计算画的点离圆心的距�?
		if (num > 0)
		{
			b--;
			a--;
		}
	}
}



//在指定位置显示一个字�?包括部分字符
//x:0~127
//y:0~31
//size:选择字体 12/16/24
void OLED_ShowChar(u8 x, u8 y, u8 chr, u8 size1)
{
	u8 i, m, temp, size2, chr1;
	u8 y0 = y;
	size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);  //得到字体一个字符对应点阵集所占的字节�?
	chr1 = chr - ' ';  //计算偏移后的�?
	for (i = 0;i < size2;i++)
	{
		if (size1 == 12)
		{
			temp = asc2_1206[chr1][i];
		} //调用1206字体
		else if (size1 == 16)
		{
			temp = asc2_1608[chr1][i];
		} //调用1608字体
		else if (size1 == 24)
		{
			temp = asc2_2412[chr1][i];
		} //调用2412字体
		else return;
		for (m = 0;m < 8;m++)           //写入数据
		{
			if (temp & 0x80)OLED_DrawPoint(x, y);
			else OLED_ClearPoint(x, y);
			temp <<= 1;
			y++;
			if ((y - y0) == size1)
			{
				y = y0;
				x++;
				break;
			}
		}
	}
}


//显示字符�?
//x,y:起点坐标  
//size1:字体大小 
//*chr:字符串起始地址 
void OLED_ShowString(u8 x, u8 y, u8* chr, u8 size1)
{
	while ((*chr >= ' ') && (*chr <= '~'))//判断是不是非法字�?
	{
		OLED_ShowChar(x, y, *chr, size1);
		x += size1 / 2U;
		if (x >= 128U)  // wrap only after the right edge is actually crossed
		{
			x = 0U;
			y += size1;
		}
		chr++;
	}
}

//m^n
u32 OLED_Pow(u8 m, u8 n)
{
	u32 result = 1;
	while (n--)
	{
		result *= m;
	}
	return result;
}

////显示2个数�?
////x,y :起点坐标	 
////len :数字的位�?
////size:字体大小
void OLED_ShowNum(u8 x, u8 y, u32 num, u8 len, u8 size1)
{
	u8 t, temp;
	for (t = 0;t < len;t++)
	{
		temp = (num / OLED_Pow(10, len - t - 1)) % 10;
		if (temp == 0)
		{
			OLED_ShowChar(x + (size1 / 2) * t, y, '0', size1);
		}
		else
		{
			OLED_ShowChar(x + (size1 / 2) * t, y, temp + '0', size1);
		}
	}
}

//显示汉字
//x,y:起点坐标
//num:汉字对应的序�?
void OLED_ShowChinese(u8 x, u8 y, u8 num, u8 size1)
{
	u8 i, m, n = 0, temp, chr1;
	u8 x0 = x, y0 = y;
	u8 size3 = size1 / 8;
	while (size3--)
	{
		chr1 = num * size1 / 8 + n;
		n++;
		for (i = 0;i < size1;i++)
		{
			if (size1 == 16)
			{
				temp = Hzk1[chr1][i];
			}//调用16*16字体
			else if (size1 == 24)
			{
				temp = Hzk2[chr1][i];
			}//调用24*24字体
			else if (size1 == 32)
			{
				temp = Hzk3[chr1][i];
			}//调用32*32字体
			else if (size1 == 64)
			{
				temp = Hzk4[chr1][i];
			}//调用64*64字体
			else return;

			for (m = 0;m < 8;m++)
			{
				if (temp & 0x01)OLED_DrawPoint(x, y);
				else OLED_ClearPoint(x, y);
				temp >>= 1;
				y++;
			}
			x++;
			if ((x - x0) == size1)
			{
				x = x0;y0 = y0 + 8;
			}
			y = y0;
		}
	}
}

//num 显示汉字的个�?
//space 每一遍显示的间隔
void OLED_ScrollDisplay(u8 num, u8 space)
{
	u8 i, n, t = 0, m = 0, r;
	while (1)
	{
		if (m == 0)
		{
			OLED_ShowChinese(128, 8, t, 16); //写入一个汉字保存在OLED_GRAM[][]数组�?
			t++;
		}
		if (t == num)
		{
			for (r = 0;r < 16 * space;r++)      //显示间隔
			{
				for (i = 0;i < 144;i++)
				{
					for (n = 0;n < 4;n++)
					{
						OLED_GRAM[i - 1][n] = OLED_GRAM[i][n];
					}
				}
				OLED_Refresh();
			}
			t = 0;
		}
		m++;
		if (m == 16) { m = 0; }
		for (i = 0;i < 144;i++)   //实现左移
		{
			for (n = 0;n < 4;n++)
			{
				OLED_GRAM[i - 1][n] = OLED_GRAM[i][n];
			}
		}
		OLED_Refresh();
	}
}

//配置写入数据的起始位�?
void OLED_WR_BP(u8 x, u8 y)
{
	OLED_WR_Byte(0xb0 + y, OLED_CMD);//设置行起始地址
	OLED_WR_Byte(((x & 0xf0) >> 4) | 0x10, OLED_CMD);
	OLED_WR_Byte((x & 0x0f) | 0x01, OLED_CMD);
}

//x0,y0：起点坐�?
//x1,y1：终点坐�?
//BMP[]：要写入的图片数�?
void OLED_ShowPicture(u8 x0, u8 y0, u8 x1, u8 y1, u8 BMP[])
{
	u32 j = 0;
	u8 x = 0, y = 0;
	for (y = y0;y < y1;y++)
	{
		OLED_WR_BP(x0, y);
		for (x = x0;x < x1;x++)
		{
			OLED_WR_Byte(BMP[j], OLED_DATA);
			j++;
		}
	}
}
/************************************************************
 * Function :       OLED_Init
 * Comment  :       1、实现端口初始化
										2、这个OLED是硬复位，要考虑时序
 * Parameter:       null
 * Return   :       null
 * Author   :       Lingyu Meng
 * Date     :       2025-04-01 V0.3 original
************************************************************/
void OLED_Init(void)
{
	/* 使能GPIOB时钟 */
	rcu_periph_clock_enable(RCU_GPIOB);

	/* 配置SCL和SDA引脚 */
	gpio_mode_set(OLED_SCL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SCL_PIN);
	gpio_output_options_set(OLED_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SCL_PIN);
	OLED_SCLK_Set();
	gpio_mode_set(OLED_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SDA_PIN);
	gpio_output_options_set(OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SDA_PIN);
	OLED_SDIN_Set();


	delay_1ms(500);

	OLED_WR_Byte(0xAE, OLED_CMD);//--关闭 OLED 面板
	OLED_WR_Byte(0x00, OLED_CMD);//---设置低列起始地址
	OLED_WR_Byte(0x10, OLED_CMD);//---设置高列起始地址
	OLED_WR_Byte(0x40, OLED_CMD);//--设置起始行地址，映�?RAM 显示起始�?(0x00~0x3F)
	OLED_WR_Byte(0x81, OLED_CMD);//--设置对比度控制寄存器
	OLED_WR_Byte(0xCF, OLED_CMD);// 设置 SEG 输出电流亮度
	OLED_WR_Byte(0xA1, OLED_CMD);//--设置 SEG/列映�?    0xa0左右反置 0xa1正常
	OLED_WR_Byte(0xC8, OLED_CMD);//设置 COM/行扫描方�?  0xc0上下反置 0xc8正常
	OLED_WR_Byte(0xA6, OLED_CMD);//--设置正常显示
	OLED_WR_Byte(0xA8, OLED_CMD);//--设置复用�?1 to 64)
	OLED_WR_Byte(0x1f, OLED_CMD);//--1/64 占空�?
	OLED_WR_Byte(0xD3, OLED_CMD);//-设置显示偏移，映�?RAM 计数器移�?(0x00~0x3F)
	OLED_WR_Byte(0x00, OLED_CMD);//-无偏�?
	OLED_WR_Byte(0xd5, OLED_CMD);//--设置显示时钟分频�?振荡器频�?
	OLED_WR_Byte(0x80, OLED_CMD);//--设置分频比，时钟设为 100 �?�?
	OLED_WR_Byte(0xD9, OLED_CMD);//--设置预充电周�?
	OLED_WR_Byte(0xF1, OLED_CMD);//预充�?15 个时钟，放电 1 个时�?
	OLED_WR_Byte(0xDA, OLED_CMD);//--设置 COM 引脚硬件配置
	OLED_WR_Byte(0x00, OLED_CMD);
	OLED_WR_Byte(0xDB, OLED_CMD);//--设置 VCOMH 电压
	OLED_WR_Byte(0x40, OLED_CMD);//设置 VCOM 取消选择电平
	OLED_WR_Byte(0x20, OLED_CMD);//-设置页寻址模式 (0x00/0x01/0x02)
	OLED_WR_Byte(0x02, OLED_CMD);//
	OLED_WR_Byte(0x8D, OLED_CMD);//--设置电荷泵使�?禁用
	OLED_WR_Byte(0x14, OLED_CMD);//--0x10 为禁�?
	OLED_WR_Byte(0xA4, OLED_CMD);// 禁用全屏显示开�?(0xa4/0xa5)
	OLED_WR_Byte(0xA6, OLED_CMD);// 禁用反色显示 (0xa6/a7)
	OLED_WR_Byte(0xAF, OLED_CMD);
	OLED_Clear();
}



/* GD_Firmware_Template_v2 compatible OLED API. */
static void OLED_ShowCharPage(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize)
{
    uint8_t c;
    uint8_t index;

    if ((ch < ' ') || (ch > '~') || (x >= OLED_WIDTH) || (y >= 4U))
    {
        return;
    }

    c = (uint8_t)(ch - ' ');

    if (fontsize == 16U)
    {
        if (y >= 3U)
        {
            return;
        }

        OLED_Set_Position(x, y);
        for (index = 0U; (index < 8U) && ((x + index) < OLED_WIDTH); index++)
        {
            OLED_Write_data(F8X16[c * 16U + index]);
        }

        OLED_Set_Position(x, (uint8_t)(y + 1U));
        for (index = 0U; (index < 8U) && ((x + index) < OLED_WIDTH); index++)
        {
            OLED_Write_data(F8X16[c * 16U + index + 8U]);
        }
    }
    else
    {
        OLED_Set_Position(x, y);
        for (index = 0U; (index < 6U) && ((x + index) < OLED_WIDTH); index++)
        {
            OLED_Write_data(F6x8[c][index]);
        }

        for (; (index < 8U) && ((x + index) < OLED_WIDTH); index++)
        {
            OLED_Write_data(0x00U);
        }
    }
}

void OLED_Write_cmd(uint8_t cmd)
{
    OLED_WR_Byte(cmd, OLED_CMD);
}

void OLED_Write_data(uint8_t data)
{
    OLED_WR_Byte(data, OLED_DATA);
}

void OLED_Set_Position(uint8_t x, uint8_t y)
{
    OLED_WR_Byte((uint8_t)(0xb0U + y), OLED_CMD);
    OLED_WR_Byte((uint8_t)(((x & 0xf0U) >> 4U) | 0x10U), OLED_CMD);
    OLED_WR_Byte((uint8_t)(x & 0x0fU), OLED_CMD);
}

void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize)
{
    uint8_t offset = 0U;
    uint8_t row_step = (fontsize == 16U) ? 2U : 1U;

    if (ch == NULL)
    {
        return;
    }

    while (ch[offset] != '\0')
    {
        OLED_ShowCharPage(x, y, (uint8_t)ch[offset], fontsize);
        x = (uint8_t)(x + 8U);
        if (x > 120U)
        {
            x = 0U;
            y = (uint8_t)(y + row_step);
            if (y >= 4U)
            {
                return;
            }
        }
        offset++;
    }
}

void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize)
{
    uint8_t offset = 0U;
    uint8_t digit_count = 0U;
    uint8_t digit_index;
    uint8_t digit;
    uint32_t integer;
    uint32_t divider = 1U;
    float decimals;

    if (num < 0.0f)
    {
        OLED_ShowCharPage(x, y, '-', fontsize);
        x = (uint8_t)(x + 8U);
        num = -num;
    }

    integer = (uint32_t)num;
    decimals = num - (float)integer;

    do
    {
        digit_count++;
        if (digit_count > 1U)
        {
            divider *= 10U;
        }
    } while ((integer / divider) >= 10U);

    for (digit_index = 0U; digit_index < digit_count; digit_index++)
    {
        digit = (uint8_t)((integer / divider) % 10U);
        OLED_ShowCharPage((uint8_t)(x + offset * 8U), y, (uint8_t)('0' + digit), fontsize);
        offset++;
        divider /= 10U;
    }

    if (accuracy == 0U)
    {
        return;
    }

    OLED_ShowCharPage((uint8_t)(x + offset * 8U), y, '.', fontsize);
    offset++;

    while (accuracy > 0U)
    {
        decimals *= 10.0f;
        digit = (uint8_t)decimals;
        OLED_ShowCharPage((uint8_t)(x + offset * 8U), y, (uint8_t)('0' + digit), fontsize);
        decimals -= (float)digit;
        offset++;
        accuracy--;
    }
}

void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[])
{
    OLED_ShowPicture(x0, y0, x1, y1, BMP);
}

void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no)
{
    OLED_ShowChinese(x, (uint8_t)(y * 8U), no, 16U);
    OLED_Refresh();
}

void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n)
{
    OLED_ShowChinese(x, (uint8_t)(y * 8U), n, 24U);
    OLED_Refresh();
}

void OLED_Allfill(void)
{
    uint8_t page;
    uint8_t column;

    for (page = 0U; page < 4U; page++)
    {
        OLED_Set_Position(0U, page);
        for (column = 0U; column < 128U; column++)
        {
            OLED_Write_data(0xFFU);
        }
    }
}

void OLED_Display_On(void)
{
    OLED_DisPlay_On();
}

void OLED_Display_Off(void)
{
    OLED_DisPlay_Off();
}
