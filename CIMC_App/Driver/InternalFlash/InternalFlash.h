#ifndef __INTERNALFLASH_H__
#define __INTERNALFLASH_H__

#include "HeaderFiles.h"

void InternalFlash_ErasePage(uint32_t addr);
void InternalFlash_EraseSector(uint32_t addr);
void InternalFlash_WriteBytes(uint32_t addr, const uint8_t *data, uint32_t len);
uint8_t InternalFlash_ReadByte(uint32_t addr);
uint32_t InternalFlash_ReadWord(uint32_t addr);

#endif
