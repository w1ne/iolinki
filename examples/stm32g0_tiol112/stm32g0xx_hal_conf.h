/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32G0XX_HAL_CONF_H
#define STM32G0XX_HAL_CONF_H

/* Optional UART/DMA transport only. Clock, GPIO and TIM2 remain BSP owned. */
#define HAL_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
#define USE_RTOS 0U
#define HSE_VALUE 8000000UL
#define HSE_STARTUP_TIMEOUT 100UL
#define HSI_VALUE 16000000UL
#define HSI48_VALUE 48000000UL
#define LSI_VALUE 32000UL
#define LSE_VALUE 32768UL
#define LSE_STARTUP_TIMEOUT 5000UL
#define EXTERNAL_I2S1_CLOCK_VALUE 48000UL
#define EXTERNAL_I2S2_CLOCK_VALUE 48000UL
#define VDD_VALUE 3300UL
#define TICK_INT_PRIORITY 3UL
#define PREFETCH_ENABLE 0U
#define INSTRUCTION_CACHE_ENABLE 0U

#include "stm32g0xx_hal_rcc.h"
#include "stm32g0xx_hal_dma.h"
#include "stm32g0xx_hal_cortex.h"
#include "stm32g0xx_hal_flash.h"
#include "stm32g0xx_hal_gpio.h"
#include "stm32g0xx_hal_pwr.h"
#include "stm32g0xx_hal_uart.h"

#define assert_param(expression) ((void) 0U)
#endif
