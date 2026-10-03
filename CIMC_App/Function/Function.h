#ifndef __FUNCTION_H
#define __FUNCTION_H

#include "HeaderFiles.h"
#include "systick.h"
#include "Led_service.h"
#include "Key_service.h"
#include "Usart_service.h"
#include "Adc_service.h"
#include "Dac_service.h"
#include "Gd30ad3344_service.h"
#include "AutoReport_service.h"
#include "Alert_service.h"
#include "Sleep_service.h"
#include "Oled_service.h"
#include "Spi_flash_service.h"
#include "BootUpdate_service.h"
#include "Rtc_service.h"

void System_Init(void);
void System_Loop(void);
void UsrFunction(void);

#endif
