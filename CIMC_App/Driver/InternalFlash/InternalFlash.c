#include "InternalFlash.h"

static void InternalFlash_ClearFlags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
}

void InternalFlash_ErasePage(uint32_t addr)
{
    fmc_unlock();
    InternalFlash_ClearFlags();
    fmc_page_erase(addr);
    InternalFlash_ClearFlags();
    fmc_lock();
}

void InternalFlash_EraseSector(uint32_t addr)
{
    fmc_unlock();
    InternalFlash_ClearFlags();
    fmc_sector_erase(addr);
    InternalFlash_ClearFlags();
    fmc_lock();
}

void InternalFlash_WriteBytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t index;

    if ((data == NULL) || (len == 0UL))
    {
        return;
    }

    fmc_unlock();
    InternalFlash_ClearFlags();

    for (index = 0UL; index < len; index++)
    {
        fmc_byte_program(addr + index, data[index]);
        while (fmc_flag_get(FMC_FLAG_BUSY) != RESET)
        {
        }
    }

    InternalFlash_ClearFlags();
    fmc_lock();
}

uint8_t InternalFlash_ReadByte(uint32_t addr)
{
    return *(volatile uint8_t *)addr;
}

uint32_t InternalFlash_ReadWord(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}
