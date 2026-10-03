#ifndef __DAC_H
#define __DAC_H

#include "HeaderFiles.h"

#define DAC_OUTPUT_MAX_RAW 4095U

/**
 * @brief Initialize the board DAC output used by the contest DAC loopback test.
 * @return None.
 *
 * @details
 * PA4 is configured as the GD32F470 native DAC0 OUT0 analog output. The contest
 * CH1 path expects PA4 to be jumper-connected to PC1, so this driver only owns
 * the output pin and leaves ADC sampling ownership in the ADC driver.
 */
void DAC_Init(void);

/**
 * @brief Set the DAC0 OUT0 holding register with a 12-bit right-aligned value.
 * @param raw_value Business DAC code in the range 0..4095. Values above 4095
 *                  are saturated to protect the 12-bit hardware register.
 * @return None.
 */
void DAC_SetRaw(uint16_t raw_value);

/**
 * @brief Read the last value written into DAC0 OUT0.
 * @return Last saturated 12-bit raw code written through DAC_SetRaw().
 */
uint16_t DAC_GetRaw(void);

#endif /* __DAC_H */
