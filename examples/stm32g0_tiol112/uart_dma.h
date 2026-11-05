/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32G0_UART_DMA_H
#define STM32G0_UART_DMA_H

#include <stddef.h>
#include <stdint.h>

#define IOLINK_G0_UART_CMSIS_IRQ 0
#define IOLINK_G0_UART_CMSIS_DMA 1
#define IOLINK_G0_UART_HAL_DMA 2
#ifndef IOLINK_G0_UART_BACKEND
#define IOLINK_G0_UART_BACKEND IOLINK_G0_UART_CMSIS_IRQ
#endif
#if IOLINK_G0_UART_BACKEND < IOLINK_G0_UART_CMSIS_IRQ || \
    IOLINK_G0_UART_BACKEND > IOLINK_G0_UART_HAL_DMA
#error "IOLINK_G0_UART_BACKEND must be 0 (CMSIS IRQ), 1 (CMSIS DMA), or 2 (HAL DMA)"
#endif

typedef struct {
    volatile uint32_t rx_bytes;
    volatile uint32_t rx_half_events;
    volatile uint32_t rx_wraps;
    volatile uint32_t rx_rearms;
    volatile uint32_t rx_overruns;
    volatile uint32_t tx_started;
    volatile uint32_t tx_dma_completed;
    volatile uint32_t tx_uart_completed;
    volatile uint32_t tx_aborts;
    volatile uint32_t dma_errors;
    volatile uint32_t uart_errors;
} board_dma_stats_t;

extern volatile uint32_t board_uart_backend;
extern board_dma_stats_t board_dma_stats;

#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
int board_dma_configure(uint32_t baud);
int board_dma_recv(uint8_t* byte, uint32_t* observed_us);
void board_dma_rx_pause(void);
int board_dma_rx_resume(void);
int board_dma_tx_begin(const uint8_t* data, size_t length);
/* 0 pending, 1 UART TC observed, -1 transfer error. The caller bounds the wait. */
int board_dma_tx_poll(void);
void board_dma_tx_abort(void);
void board_dma_stop(void);
void board_dma_uart_irq(void);
#endif

#endif
