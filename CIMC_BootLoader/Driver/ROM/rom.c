/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：usart.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "rom.h"

/************************* 宏定义 *************************/


/************************ 变量定义 ************************/


/************************ 函数定义 ************************/

/************************************************************
 * Function :       internal_flash_erase
 * Comment  :       用于内部flash页(4KB)擦除
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
//!内部flash页(4KB)擦除
void internal_flash_erase(uint32_t addr)
{

	fmc_unlock();  //fmc解锁
	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	//页擦除
	fmc_page_erase(addr);

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_earse_sector
 * Comment  :       用于内部flash扇区擦除
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_earse_sector(uint32_t addr)
{
	fmc_unlock();  //fmc解锁
	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	//扇擦除
	fmc_sector_erase(addr);

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write
 * Comment  :       用于内部flash半字写入
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
//!半字写入
void internal_flash_write(uint32_t addr , uint16_t data)
{
	fmc_unlock();

	fmc_halfword_program(addr , data);

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write_word
 * Comment  :       用于内部flash字写入
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
//!半字写入
void internal_flash_write_word(uint32_t addr , uint32_t data)
{
	fmc_unlock();

	fmc_word_program(addr , data);

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}

/************************************************************
 * Function :       internal_flash_write_str
 * Comment  :       用于内部flash字符串写入
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write_str(uint32_t addr , uint16_t* data , uint16_t len)
{
	fmc_unlock();

	for (uint16_t i = 0; i < len; i++)
	{
		fmc_halfword_program(addr + i * 2 , data[i]);
	}

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_write_str_Char
 * Comment  :       用于内部flash字符串写入(字符型)
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
void internal_flash_write_str_Char(uint32_t addr , uint8_t* data , uint32_t len)
{
	fmc_unlock();

	for (uint32_t i = 0; i < len; i++)
	{
		fmc_byte_program(addr + i , data[i]);
		while(fmc_flag_get(FMC_FLAG_BUSY));
		// printf("Progress is %d%%\r\n" , (i * 100 / len));
	}

	fmc_flag_clear(FMC_FLAG_END); //清除FMC忙碌标志位
	fmc_flag_clear(FMC_FLAG_WPERR);
	fmc_flag_clear(FMC_FLAG_PGSERR);
	fmc_flag_clear(FMC_FLAG_PGMERR);

	fmc_lock();
}
/************************************************************
 * Function :       internal_flash_read
 * Comment  :       用于内部flash半字读取
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint16_t internal_flash_read(uint32_t addr)
{
	return *(volatile uint16_t*)(addr);
}
/************************************************************
 * Function :       internal_flash_read_Char
 * Comment  :       用于内部flash字符型读取
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint8_t internal_flash_read_Char(uint32_t addr)
{
	return *(volatile uint8_t*)(addr);
}

/************************************************************
 * Function :       internal_flash_read_word
 * Comment  :       用于内部flash字读取
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-2-4 V0.1 original
************************************************************/
uint32_t internal_flash_read_word(uint32_t addr)
{
	return *(volatile uint32_t*)(addr);
}

/****************************End*****************************/
