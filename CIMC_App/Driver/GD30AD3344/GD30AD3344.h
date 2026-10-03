/************************************************************
 * Copyright: 2025 CIMC
 * File     : GD30AD3344.h
 * Brief    : GD30AD3344 external ADC driver on SPI3.
 ************************************************************/
#ifndef __GD30AD3344_H
#define __GD30AD3344_H

#include "gd32f4xx.h"
#include "gd32f4xx_libopt.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board mapping: PE12=SCK, PE13=MISO, PE14=MOSI, PE10=CS. */
#define GD30AD3344_SPI             SPI3
#define GD30AD3344_SPI_RCU         RCU_SPI3
#define GD30AD3344_GPIO_PORT       GPIOE
#define GD30AD3344_GPIO_RCU        RCU_GPIOE
#define GD30AD3344_SCK_PIN         GPIO_PIN_12
#define GD30AD3344_MISO_PIN        GPIO_PIN_13
#define GD30AD3344_MOSI_PIN        GPIO_PIN_14
#define GD30AD3344_CS_PIN          GPIO_PIN_10
#define GD30AD3344_GPIO_AF         GPIO_AF_5

#define GD30AD3344_DMA             DMA1
#define GD30AD3344_DMA_RCU         RCU_DMA1
#define GD30AD3344_DMA_TX_CH       DMA_CH1
#define GD30AD3344_DMA_RX_CH       DMA_CH0
#define GD30AD3344_DMA_SUBPERI     DMA_SUBPERI4

#define GD30AD3344_CS_LOW()        gpio_bit_reset(GD30AD3344_GPIO_PORT, GD30AD3344_CS_PIN)
#define GD30AD3344_CS_HIGH()       gpio_bit_set(GD30AD3344_GPIO_PORT, GD30AD3344_CS_PIN)

typedef struct
{
    uint16_t SS         : 1;
    uint16_t MUX        : 3;
    uint16_t PGA        : 3;
    uint16_t MODE       : 1;
    uint16_t DR         : 3;
    uint16_t RESERVED_1 : 1;
    uint16_t PULL_UP_EN : 1;
    uint16_t NOP        : 2;
    uint16_t RESERVED   : 1;
} GD30AD3344_Config_t;

typedef enum
{
    GD30AD3344_OS_DISABLE        = 0,
    GD30AD3344_OS_SINGLE_CONVERT = 1,
} GD30AD3344_OS_TypeDef;

typedef enum
{
    GD30AD3344_Channel_0 = 0, /* AINP=AIN0, AINN=AIN1 */
    GD30AD3344_Channel_1 = 1, /* AINP=AIN0, AINN=AIN3 */
    GD30AD3344_Channel_2 = 2, /* AINP=AIN1, AINN=AIN3 */
    GD30AD3344_Channel_3 = 3, /* AINP=AIN2, AINN=AIN3 */
    GD30AD3344_Channel_4 = 4, /* AINP=AIN0, AINN=GND  */
    GD30AD3344_Channel_5 = 5, /* AINP=AIN1, AINN=GND  */
    GD30AD3344_Channel_6 = 6, /* AINP=AIN2, AINN=GND  */
    GD30AD3344_Channel_7 = 7, /* AINP=AIN3, AINN=GND  */
} GD30AD3344_Channel_TypeDef;

#define GD30AD3344_MUX_AIN0_AIN1  GD30AD3344_Channel_0
#define GD30AD3344_MUX_AIN0_AIN3  GD30AD3344_Channel_1
#define GD30AD3344_MUX_AIN1_AIN3  GD30AD3344_Channel_2
#define GD30AD3344_MUX_AIN2_AIN3  GD30AD3344_Channel_3
#define GD30AD3344_MUX_AIN0_GND   GD30AD3344_Channel_4
#define GD30AD3344_MUX_AIN1_GND   GD30AD3344_Channel_5
#define GD30AD3344_MUX_AIN2_GND   GD30AD3344_Channel_6
#define GD30AD3344_MUX_AIN3_GND   GD30AD3344_Channel_7

typedef enum
{
    GD30AD3344_PGA_6V144    = 0,
    GD30AD3344_PGA_4V096    = 1,
    GD30AD3344_PGA_2V048    = 2,
    GD30AD3344_PGA_1V024    = 3,
    GD30AD3344_PGA_0V512    = 4,
    GD30AD3344_PGA_0V256    = 5,
    GD30AD3344_PGA_0V064    = 6,
    GD30AD3344_PGA_RESERVED = 7,
} GD30AD3344_PGA_TypeDef;

typedef enum
{
    GD30AD3344_MODE_CONTINUOUS  = 0,
    GD30AD3344_MODE_SINGLE_SHOT = 1,
} GD30AD3344_Mode_TypeDef;

typedef enum
{
    GD30AD3344_DR_6_25SPS  = 0,
    GD30AD3344_DR_12_5SPS  = 1,
    GD30AD3344_DR_25SPS    = 2,
    GD30AD3344_DR_50SPS    = 3,
    GD30AD3344_DR_100SPS   = 4,
    GD30AD3344_DR_250SPS   = 5,
    GD30AD3344_DR_500SPS   = 6,
    GD30AD3344_DR_1000SPS  = 7,
} GD30AD3344_DR_TypeDef;

typedef enum
{
    GD30AD3344_PULL_UP_DISABLE = 0,
    GD30AD3344_PULL_UP_ENABLE  = 1,
} GD30AD3344_PullUp_TypeDef;

typedef enum
{
    GD30AD3344_NOP_INVALID_NO_UPDATE = 0,
    GD30AD3344_NOP_VALID_UPDATE      = 1,
    GD30AD3344_NOP_VALID_NO_UPDATE   = 2,
    GD30AD3344_NOP_INVALID_NO_CHANGE = 3,
} GD30AD3344_NOP_TypeDef;

#define GD30AD3344_RESERVED_0 0U
#define GD30AD3344_RESERVED_1 1U

extern GD30AD3344_Config_t GD30AD3344_InitStruct;

void GD30AD3344_Init(void);
float GD30AD3344_AD_Read(GD30AD3344_Channel_TypeDef channel, GD30AD3344_PGA_TypeDef range);
uint8_t GD30AD3344_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __GD30AD3344_H */
