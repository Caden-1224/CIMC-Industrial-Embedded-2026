#ifndef __DAC_SERVICE_H
#define __DAC_SERVICE_H

#include "HeaderFiles.h"

/**
 * @brief Initialize the DAC business service.
 * @return None.
 *
 * @details
 * This service currently owns only the contest DAC output value. The lower
 * driver configures PA4/DAC0 OUT0; this layer provides the business-facing API
 * used by command 0x0301 and by local diagnostics.
 */
void Dac_Service_Init(void);

/**
 * @brief Set contest DAC output as a raw 12-bit code.
 * @param raw_value Requested DAC code from command payload, range 0..4095.
 *                  Values above 4095 are saturated by the hardware driver.
 * @return None.
 */
void Dac_Service_SetRaw(uint16_t raw_value);

/**
 * @brief Get the last DAC output code accepted by the service.
 * @return Last 12-bit raw DAC code after saturation.
 */
uint16_t Dac_Service_GetRaw(void);

#endif /* __DAC_SERVICE_H */
