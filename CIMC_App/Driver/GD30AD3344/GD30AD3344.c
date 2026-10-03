/************************************************************
 * Copyright: 2025 CIMC
 * File     : GD30AD3344.c
 * Brief    : GD30AD3344 external ADC driver on SPI3 with DMA.
 ************************************************************/
#include "GD30AD3344.h"

#define GD30AD3344_DMA_BUFFER_SIZE 12U
#define GD30AD3344_DMA_TIMEOUT     1000000UL
#define GD30AD3344_SPI_TIMEOUT     1000000UL

static uint8_t s_spi_tx_buffer[GD30AD3344_DMA_BUFFER_SIZE];
static uint8_t s_spi_rx_buffer[GD30AD3344_DMA_BUFFER_SIZE];
static uint8_t s_gd30ad3344_ready = 0U;

GD30AD3344_Config_t GD30AD3344_InitStruct;

static uint16_t GD30AD3344_BuildConfigWord(void)
{
    return (uint16_t)((GD30AD3344_InitStruct.SS << 15) |
                      (GD30AD3344_InitStruct.MUX << 12) |
                      (GD30AD3344_InitStruct.PGA << 9) |
                      (GD30AD3344_InitStruct.MODE << 8) |
                      (GD30AD3344_InitStruct.DR << 5) |
                      (GD30AD3344_InitStruct.RESERVED_1 << 4) |
                      (GD30AD3344_InitStruct.PULL_UP_EN << 3) |
                      (GD30AD3344_InitStruct.NOP << 1) |
                      (GD30AD3344_InitStruct.RESERVED << 0));
}

static float GD30AD3344_GetRangeVoltage(GD30AD3344_PGA_TypeDef range)
{
    switch (range)
    {
    case GD30AD3344_PGA_6V144:
        return 6.144f;
    case GD30AD3344_PGA_4V096:
        return 4.096f;
    case GD30AD3344_PGA_2V048:
        return 2.048f;
    case GD30AD3344_PGA_1V024:
        return 1.024f;
    case GD30AD3344_PGA_0V512:
        return 0.512f;
    case GD30AD3344_PGA_0V256:
        return 0.256f;
    case GD30AD3344_PGA_0V064:
        return 0.064f;
    default:
        return 2.048f;
    }
}

static uint8_t GD30AD3344_WaitDmaDone(void)
{
    uint32_t timeout = GD30AD3344_DMA_TIMEOUT;

    while (RESET == dma_flag_get(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH, DMA_FLAG_FTF))
    {
        if (timeout == 0UL)
        {
            return 0U;
        }
        timeout--;
    }

    timeout = GD30AD3344_SPI_TIMEOUT;
    while (SET == spi_i2s_flag_get(GD30AD3344_SPI, SPI_FLAG_TRANS))
    {
        if (timeout == 0UL)
        {
            return 0U;
        }
        timeout--;
    }

    return 1U;
}

static void GD30AD3344_StopDma(void)
{
    spi_dma_disable(GD30AD3344_SPI, SPI_DMA_RECEIVE);
    spi_dma_disable(GD30AD3344_SPI, SPI_DMA_TRANSMIT);
    dma_channel_disable(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH);
    dma_channel_disable(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH);
    dma_flag_clear(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH, DMA_FLAG_FTF);
    dma_flag_clear(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH, DMA_FLAG_FTF);
}

static uint8_t GD30AD3344_TransferDma(const uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t size)
{
    dma_single_data_parameter_struct dma_init_struct;
    uint16_t index;
    uint8_t transfer_ok;

    if ((tx_buffer == NULL) || (rx_buffer == NULL) || (size == 0U))
    {
        return 0U;
    }

    if (size > GD30AD3344_DMA_BUFFER_SIZE)
    {
        size = GD30AD3344_DMA_BUFFER_SIZE;
    }

    for (index = 0U; index < size; index++)
    {
        s_spi_tx_buffer[index] = tx_buffer[index];
        s_spi_rx_buffer[index] = 0U;
    }

    GD30AD3344_StopDma();

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_deinit(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH);
    dma_init_struct.periph_addr = (uint32_t)&SPI_DATA(GD30AD3344_SPI);
    dma_init_struct.memory0_addr = (uint32_t)s_spi_tx_buffer;
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number = size;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH, &dma_init_struct);
    dma_channel_subperipheral_select(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH, GD30AD3344_DMA_SUBPERI);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_deinit(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH);
    dma_init_struct.periph_addr = (uint32_t)&SPI_DATA(GD30AD3344_SPI);
    dma_init_struct.memory0_addr = (uint32_t)s_spi_rx_buffer;
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number = size;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH, &dma_init_struct);
    dma_channel_subperipheral_select(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH, GD30AD3344_DMA_SUBPERI);

    dma_channel_enable(GD30AD3344_DMA, GD30AD3344_DMA_RX_CH);
    dma_channel_enable(GD30AD3344_DMA, GD30AD3344_DMA_TX_CH);
    spi_dma_enable(GD30AD3344_SPI, SPI_DMA_RECEIVE);
    spi_dma_enable(GD30AD3344_SPI, SPI_DMA_TRANSMIT);

    transfer_ok = GD30AD3344_WaitDmaDone();
    GD30AD3344_StopDma();

    if (transfer_ok == 0U)
    {
        return 0U;
    }

    for (index = 0U; index < size; index++)
    {
        rx_buffer[index] = s_spi_rx_buffer[index];
    }

    return 1U;
}

static uint16_t GD30AD3344_SendHalfwordDma(uint16_t half_word)
{
    uint8_t tx_buffer[2];
    uint8_t rx_buffer[2] = {0U, 0U};

    tx_buffer[0] = (uint8_t)(half_word >> 8);
    tx_buffer[1] = (uint8_t)half_word;

    GD30AD3344_CS_LOW();
    if (GD30AD3344_TransferDma(tx_buffer, rx_buffer, 2U) == 0U)
    {
        GD30AD3344_CS_HIGH();
        s_gd30ad3344_ready = 0U;
        return 0U;
    }
    GD30AD3344_CS_HIGH();

    return (uint16_t)(((uint16_t)rx_buffer[0] << 8) | rx_buffer[1]);
}

static void GD30AD3344_PortInit(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(GD30AD3344_GPIO_RCU);
    rcu_periph_clock_enable(GD30AD3344_SPI_RCU);
    rcu_periph_clock_enable(GD30AD3344_DMA_RCU);

    gpio_af_set(GD30AD3344_GPIO_PORT,
                GD30AD3344_GPIO_AF,
                GD30AD3344_SCK_PIN | GD30AD3344_MISO_PIN | GD30AD3344_MOSI_PIN);
    gpio_mode_set(GD30AD3344_GPIO_PORT,
                  GPIO_MODE_AF,
                  GPIO_PUPD_NONE,
                  GD30AD3344_SCK_PIN | GD30AD3344_MISO_PIN | GD30AD3344_MOSI_PIN);
    gpio_output_options_set(GD30AD3344_GPIO_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            GD30AD3344_SCK_PIN | GD30AD3344_MISO_PIN | GD30AD3344_MOSI_PIN);

    gpio_mode_set(GD30AD3344_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GD30AD3344_CS_PIN);
    gpio_output_options_set(GD30AD3344_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GD30AD3344_CS_PIN);
    GD30AD3344_CS_HIGH();

    spi_i2s_deinit(GD30AD3344_SPI);
    spi_struct_para_init(&spi_init_struct);
    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_2EDGE;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.prescale = SPI_PSC_256;
    spi_init_struct.endian = SPI_ENDIAN_MSB;
    spi_init(GD30AD3344_SPI, &spi_init_struct);
    spi_enable(GD30AD3344_SPI);
}

void GD30AD3344_Init(void)
{
    GD30AD3344_PortInit();

    GD30AD3344_InitStruct.SS = GD30AD3344_OS_DISABLE;
    GD30AD3344_InitStruct.MUX = GD30AD3344_MUX_AIN0_GND;
    GD30AD3344_InitStruct.PGA = GD30AD3344_PGA_2V048;
    GD30AD3344_InitStruct.MODE = GD30AD3344_MODE_CONTINUOUS;
    GD30AD3344_InitStruct.DR = GD30AD3344_DR_25SPS;
    GD30AD3344_InitStruct.RESERVED_1 = GD30AD3344_RESERVED_0;
    GD30AD3344_InitStruct.PULL_UP_EN = GD30AD3344_PULL_UP_DISABLE;
    GD30AD3344_InitStruct.NOP = GD30AD3344_NOP_VALID_UPDATE;
    GD30AD3344_InitStruct.RESERVED = GD30AD3344_RESERVED_1;

    s_gd30ad3344_ready = 1U;
    (void)GD30AD3344_SendHalfwordDma(GD30AD3344_BuildConfigWord());
}

float GD30AD3344_AD_Read(GD30AD3344_Channel_TypeDef channel, GD30AD3344_PGA_TypeDef range)
{
    uint16_t raw_data;

    if (s_gd30ad3344_ready == 0U)
    {
        return 0.0f;
    }

    GD30AD3344_InitStruct.MUX = channel;
    GD30AD3344_InitStruct.PGA = range;

    raw_data = GD30AD3344_SendHalfwordDma(GD30AD3344_BuildConfigWord());
    return ((float)((int16_t)raw_data) * GD30AD3344_GetRangeVoltage(range)) / 32768.0f;
}

uint8_t GD30AD3344_IsReady(void)
{
    return s_gd30ad3344_ready;
}
