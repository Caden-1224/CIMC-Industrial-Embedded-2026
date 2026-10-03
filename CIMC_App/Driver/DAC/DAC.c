#include "DAC.h"

static uint16_t s_dac_raw_value = 0U;

void DAC_Init(void)
{
    /*
     * Step 1: Enable GPIOA and DAC clocks before touching their registers.
     * PA4 belongs to GPIOA, while DAC0 is clocked from APB1.
     */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_DAC);

    /*
     * Step 2: Reserve PA4 for analog output. Analog mode disconnects the
     * digital input/output path, avoiding leakage and pin-function conflict
     * with the CH1 loopback requirement.
     */
    gpio_mode_set(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_4);

    /*
     * Step 3: Reset DAC0 into a known state, then configure OUT0 for direct
     * DC output. Command 0x0301 represents a static voltage code, so disabling
     * trigger gating makes each data-register write update the analog output
     * immediately and avoids depending on a software-trigger edge.
     */
    dac_deinit(DAC0);
    dac_trigger_disable(DAC0, DAC_OUT0);
    dac_wave_mode_config(DAC0, DAC_OUT0, DAC_WAVE_DISABLE);
    dac_output_buffer_enable(DAC0, DAC_OUT0);
    dac_enable(DAC0, DAC_OUT0);

    /*
     * Step 4: Drive a deterministic 0V default. This keeps CH1 readback stable
     * until the 0x0301 command or local service layer writes a contest value.
     */
    DAC_SetRaw(0U);
}

void DAC_SetRaw(uint16_t raw_value)
{
    if (raw_value > DAC_OUTPUT_MAX_RAW)
    {
        raw_value = DAC_OUTPUT_MAX_RAW;
    }

    s_dac_raw_value = raw_value;
    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, raw_value);
}

uint16_t DAC_GetRaw(void)
{
    return s_dac_raw_value;
}
