/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dma_rx_ring.h"
#include <stddef.h>

void board_dma_rx_ring_reset(board_dma_rx_ring_t* ring)
{
    if (ring != NULL) {
        ring->epoch = 0U;
        ring->consumed = 0U;
        ring->observed = 0U;
        ring->observed_us = 0U;
        ring->failed = false;
    }
}

int board_dma_rx_ring_event(board_dma_rx_ring_t* ring, bool half, bool complete)
{
    if (ring == NULL || ring->failed) return -1;
    /* Sticky HT+TC cannot count arbitrarily many unserviced wraps. Reject the
     * ambiguous observation instead of silently turning a lost lap into empty.
     * This also deliberately rejects some half-ring IRQ-latency cases. */
    if (half && complete) {
        ring->failed = true;
        return -1;
    }
    if (complete) ring->epoch += BOARD_DMA_RX_SIZE;
    return 0;
}

int board_dma_rx_ring_pop(board_dma_rx_ring_t* ring, const volatile uint8_t* data,
                          uint32_t remaining, uint32_t now_us, uint8_t* byte)
{
    if (ring == NULL || data == NULL || byte == NULL || ring->failed) return -1;
    if (remaining > BOARD_DMA_RX_SIZE) {
        ring->failed = true;
        return -1;
    }
    /* CNDTR can briefly read zero at reload; epoch already includes that TC.
     * Both zero and the reloaded size therefore denote the new ring origin. */
    const uint32_t position = (BOARD_DMA_RX_SIZE - remaining) & (BOARD_DMA_RX_SIZE - 1U);
    const uint32_t produced = ring->epoch + position;
    const uint32_t available = produced - ring->consumed;
    if (available >= BOARD_DMA_RX_SIZE) {
        ring->failed = true;
        return -1;
    }
    if (produced != ring->observed) {
        ring->observed = produced;
        ring->observed_us = now_us;
    }
    if (available == 0U) return 0;
    *byte = data[ring->consumed & (BOARD_DMA_RX_SIZE - 1U)];
    ++ring->consumed;
    return 1;
}
