/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32G0_DMA_RX_RING_H
#define STM32G0_DMA_RX_RING_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_DMA_RX_SIZE 128U

/* The hardware adapter supplies a coherent epoch/CNDTR observation. Keep one
 * slot spare: a full ring is an error, never indistinguishable from empty. */
typedef struct
{
    uint32_t epoch;
    uint32_t consumed;
    uint32_t observed;
    uint32_t observed_us;
    bool failed;
} board_dma_rx_ring_t;

void board_dma_rx_ring_reset(board_dma_rx_ring_t* ring);
int board_dma_rx_ring_event(board_dma_rx_ring_t* ring, bool half, bool complete);
int board_dma_rx_ring_pop(board_dma_rx_ring_t* ring, const volatile uint8_t* data,
                          uint32_t remaining, uint32_t now_us, uint8_t* byte);

#endif
