#include "Adc_service.h"
#include "ADC.h"

/************************** Macro Definitions **************************/

#define ADC_VREF_MV             3300U
#define ADC_RESOLUTION          4095U
#define ADC_SMOOTH_ALPHA        8U

/************************** Variable Definitions **************************/

Adc_Result_t adc_result = {0};

static uint16_t s_ch0_smooth_buf[ADC_SMOOTH_ALPHA] = {0};
static uint16_t s_ch1_smooth_buf[ADC_SMOOTH_ALPHA] = {0};
static uint8_t s_smooth_idx = 0U;
static uint32_t s_ch0_smooth_sum = 0U;
static uint32_t s_ch1_smooth_sum = 0U;

/************************** Static Function Declarations **************************/

static uint16_t Adc_ReadRaw(uint8_t channel_index);
static uint16_t Adc_ConvertRawToMillivolt(uint16_t raw_value);
static void Adc_Service_UpdateChannel(Adc_Channel_Result_t *result,
                                      uint16_t raw_value,
                                      uint16_t smooth_buf[ADC_SMOOTH_ALPHA],
                                      uint32_t *smooth_sum);
static void Adc_Service_PrimeChannel(Adc_Channel_Result_t *result,
                                     uint8_t channel_index,
                                     uint16_t smooth_buf[ADC_SMOOTH_ALPHA],
                                     uint32_t *smooth_sum);
static void Adc_Service_UpdateInternalAdc(void);

/************************** Function Definitions **************************/

/**
 * @brief Read the latest raw internal ADC0 sample from a fixed DMA/rank index.
 * @param channel_index Internal ADC channel index, usually ADC_INTERNAL_CH0_INDEX or ADC_INTERNAL_CH1_INDEX.
 * @return uint16_t Raw ADC code, range 0..4095.
 */
static uint16_t Adc_ReadRaw(uint8_t channel_index)
{
    if (channel_index == ADC_INTERNAL_CH0_INDEX)
    {
        return ADC_ReadChannelRaw(ADC_CHANNEL_10);
    }

    if (channel_index == ADC_INTERNAL_CH1_INDEX)
    {
        return ADC_ReadChannelRaw(ADC_CHANNEL_11);
    }

    return 0U;
}

/**
 * @brief Convert one raw ADC code to millivolts using the board reference voltage.
 * @param raw_value Raw 12-bit ADC code, range 0..4095.
 * @return uint16_t Voltage in millivolts.
 */
static uint16_t Adc_ConvertRawToMillivolt(uint16_t raw_value)
{
    return (uint16_t)(((uint32_t)raw_value * ADC_VREF_MV) / ADC_RESOLUTION);
}

/**
 * @brief Refresh one business channel result and its moving average.
 * @param result Output channel result structure to update.
 * @param raw_value Latest raw ADC code for the channel.
 * @param smooth_buf Channel-owned moving-average ring buffer.
 * @param smooth_sum Channel-owned moving-average accumulated sum.
 * @return void No return value.
 */
static void Adc_Service_UpdateChannel(Adc_Channel_Result_t *result,
                                      uint16_t raw_value,
                                      uint16_t smooth_buf[ADC_SMOOTH_ALPHA],
                                      uint32_t *smooth_sum)
{
    result->raw = raw_value;
    result->voltage = Adc_ConvertRawToMillivolt(raw_value);

    /*
     * 每个业务通道维护独立滑动平均。
     * 这样 CH0 电位器的大幅转动不会污染 CH1 DAC 回读，后续协议上报也能保持通道隔离。
     */
    *smooth_sum -= smooth_buf[s_smooth_idx];
    smooth_buf[s_smooth_idx] = result->voltage;
    *smooth_sum += smooth_buf[s_smooth_idx];

    result->smooth = (uint16_t)(*smooth_sum / ADC_SMOOTH_ALPHA);
}

/**
 * @brief Initialize one channel result and pre-fill its moving-average buffer.
 * @param result Output channel result structure to initialize.
 * @param channel_index Internal ADC channel index used to read the initial sample.
 * @param smooth_buf Channel-owned moving-average ring buffer.
 * @param smooth_sum Channel-owned moving-average accumulated sum.
 * @return void No return value.
 */
static void Adc_Service_PrimeChannel(Adc_Channel_Result_t *result,
                                     uint8_t channel_index,
                                     uint16_t smooth_buf[ADC_SMOOTH_ALPHA],
                                     uint32_t *smooth_sum)
{
    uint16_t init_voltage_mv;
    uint8_t index;

    result->raw = Adc_ReadRaw(channel_index);
    result->voltage = Adc_ConvertRawToMillivolt(result->raw);
    result->smooth = result->voltage;
    init_voltage_mv = result->voltage;

    for (index = 0U; index < ADC_SMOOTH_ALPHA; index++)
    {
        smooth_buf[index] = init_voltage_mv;
    }

    *smooth_sum = (uint32_t)init_voltage_mv * ADC_SMOOTH_ALPHA;
}

/**
 * @brief Refresh internal ADC0 CH0/CH1 raw, voltage, and moving-average values.
 * @return void No return value.
 */
static void Adc_Service_UpdateInternalAdc(void)
{
    Adc_Service_UpdateChannel(&adc_result.ch0,
                              Adc_ReadRaw(ADC_INTERNAL_CH0_INDEX),
                              s_ch0_smooth_buf,
                              &s_ch0_smooth_sum);

    /*
     * CH0 和 CH1 共用同一个采样时刻索引。
     * 当前调度任务每次都会同时更新两路，因此统一推进 ring index 可以保证两路平滑窗口时间对齐。
     */
    Adc_Service_UpdateChannel(&adc_result.ch1,
                              Adc_ReadRaw(ADC_INTERNAL_CH1_INDEX),
                              s_ch1_smooth_buf,
                              &s_ch1_smooth_sum);

    s_smooth_idx = (uint8_t)((s_smooth_idx + 1U) % ADC_SMOOTH_ALPHA);
}

void Adc_Service_Init(void)
{
    ADC_port_init();
    delay_1ms(50);

    Adc_Service_PrimeChannel(&adc_result.ch0,
                             ADC_INTERNAL_CH0_INDEX,
                             s_ch0_smooth_buf,
                             &s_ch0_smooth_sum);
    Adc_Service_PrimeChannel(&adc_result.ch1,
                             ADC_INTERNAL_CH1_INDEX,
                             s_ch1_smooth_buf,
                             &s_ch1_smooth_sum);
    s_smooth_idx = 0U;

    Adc_Service_UpdateInternalAdc();
}

void Adc_Service_Task(void)
{
    Adc_Service_UpdateInternalAdc();
}
