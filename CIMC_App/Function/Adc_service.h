/************************************************************
 * Copyright: 2025 CIMC
 * File     : Adc_service.h
 * Brief    : Application-level internal ADC acquisition service.
 ************************************************************/
#ifndef __ADC_SERVICE_H
#define __ADC_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief Single internal ADC channel acquisition result.
 * @param raw Raw 12-bit ADC code, range 0..4095.
 * @param voltage Converted voltage in millivolts.
 * @param smooth Moving-average filtered voltage in millivolts.
 */
typedef struct
{
    uint16_t raw;
    uint16_t voltage;
    uint16_t smooth;
} Adc_Channel_Result_t;

/**
 * @brief Internal ADC0 acquisition result for contest CH0/CH1.
 * @param ch0 CH0 board potentiometer sample from PC0/ADC_CHANNEL_10.
 * @param ch1 CH1 DAC feedback sample from PC1/ADC_CHANNEL_11.
 */
typedef struct
{
    Adc_Channel_Result_t ch0;
    Adc_Channel_Result_t ch1;
} Adc_Result_t;

extern Adc_Result_t adc_result;

/**
 * @brief Initialize the internal ADC acquisition resources.
 * @return void No return value.
 */
void Adc_Service_Init(void);

/**
 * @brief Periodic internal ADC acquisition task.
 * @return void No return value.
 */
void Adc_Service_Task(void);

#endif
