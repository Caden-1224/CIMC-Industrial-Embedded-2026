#ifndef __BOOTUPDATE_SERVICE_H__
#define __BOOTUPDATE_SERVICE_H__

#include "HeaderFiles.h"

void BootUpdate_Service_Init(void);
void BootUpdate_Service_Task(void);
uint8_t BootUpdate_Service_HandleFrame(const uint8_t *frame, uint16_t frame_len);
uint8_t BootUpdate_Service_RequestEnterBootloader(void);

#endif
