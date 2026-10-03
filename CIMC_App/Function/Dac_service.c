#include "Dac_service.h"
#include "DAC.h"
#include "App_param_service.h"

static uint16_t s_service_dac_raw = 0U;

void Dac_Service_Init(void)
{
    /*
     * Bring up the hardware channel first, then restore the last contest DAC
     * excitation from the shared APP parameter block. CH1 is measured through
     * the PA4->PC1 loopback, so keeping DAC at 0V after reset would make F-02
     * look like CH1 ratio persistence failed even when the ratio was saved.
     */
    DAC_Init();
    DAC_SetRaw(AppParam_Service_GetDacRaw());
    s_service_dac_raw = DAC_GetRaw();
}

void Dac_Service_SetRaw(uint16_t raw_value)
{
    /*
     * Keep the same saturation rule at the business boundary. The driver
     * repeats the clamp as a hardware-safety backstop.
     */
    if (raw_value > DAC_OUTPUT_MAX_RAW)
    {
        raw_value = DAC_OUTPUT_MAX_RAW;
    }

    DAC_SetRaw(raw_value);
    s_service_dac_raw = DAC_GetRaw();
}

uint16_t Dac_Service_GetRaw(void)
{
    return s_service_dac_raw;
}
