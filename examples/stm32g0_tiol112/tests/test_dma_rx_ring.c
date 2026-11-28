/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dma_rx_ring.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(bool condition, const char* expression, int line)
{
    ++checks;
    if (condition) return;
    fprintf(stderr, "DMA ring check failed at line %d: %s\n", line, expression);
    exit(1);
}
#define CHECK(condition) check((condition), #condition, __LINE__)

int main(void)
{
    board_dma_rx_ring_t ring;
    volatile uint8_t data[BOARD_DMA_RX_SIZE] = {0};
    uint8_t byte = 0;
    board_dma_rx_ring_reset(&ring);
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 10U, &byte) == 0);

    /* Streams spanning repeated circular reloads. Verify byte order and that
     * the immediately following empty poll does not change the timestamp. */
    for (uint32_t prefix = 0U; prefix < 4U; ++prefix) {
        board_dma_rx_ring_reset(&ring);
        for (uint32_t n = 1U; n <= 260U + prefix; ++n) {
            const uint32_t position = n & (BOARD_DMA_RX_SIZE - 1U);
            data[(n - 1U) & (BOARD_DMA_RX_SIZE - 1U)] = (uint8_t) (n ^ 0x5aU);
            if (position == 64U) CHECK(board_dma_rx_ring_event(&ring, true, false) == 0);
            if (position == 0U) CHECK(board_dma_rx_ring_event(&ring, false, true) == 0);
            uint32_t remaining = 128U - position;
            CHECK(board_dma_rx_ring_pop(&ring, data, remaining, n * 48U, &byte) == 1);
            CHECK(byte == (uint8_t) (n ^ 0x5aU));
            CHECK(ring.observed_us == n * 48U);
            CHECK(board_dma_rx_ring_pop(&ring, data, remaining, n * 48U + 1U, &byte) == 0);
            CHECK(ring.observed_us == n * 48U); /* Empty polling is not an arrival. */
        }
    }

    /* A full lap must never alias an empty ring. Even serviced IRQs cannot
     * save unread data once producer-consumer distance reaches capacity. */
    board_dma_rx_ring_reset(&ring);
    CHECK(board_dma_rx_ring_event(&ring, true, false) == 0);
    CHECK(board_dma_rx_ring_event(&ring, false, true) == 0);
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 7000U, &byte) == -1);
    CHECK(ring.failed);
    board_dma_rx_ring_reset(&ring);
    CHECK(board_dma_rx_ring_event(&ring, true, true) == -1);
    CHECK(board_dma_rx_ring_pop(&ring, data, 127U, 1U, &byte) == -1);

    /* Unsigned epoch/consumer rollover is safe. CNDTR zero and its reloaded
     * value both represent the same accounted TC boundary. */
    board_dma_rx_ring_reset(&ring);
    ring.epoch = 0xffffff80U;
    ring.consumed = 0xffffffffU;
    ring.observed = 0xffffffffU;
    data[127] = 0x67U;
    CHECK(board_dma_rx_ring_event(&ring, false, true) == 0);
    CHECK(ring.epoch == 0U);
    CHECK(board_dma_rx_ring_pop(&ring, data, 0U, 0xfffffff0U, &byte) == 1);
    CHECK(byte == 0x67U && ring.consumed == 0U);
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 3U, &byte) == 0);
    data[0] = 0xe1U;
    CHECK(board_dma_rx_ring_pop(&ring, data, 127U, 20U, &byte) == 1);
    CHECK(byte == 0xe1U && ring.observed_us == 20U);

    /* Hardware impossibilities and unaccounted reloads fail closed. Reset
     * discards old contents and permits exactly the new producer history. */
    board_dma_rx_ring_reset(&ring);
    CHECK(board_dma_rx_ring_pop(&ring, data, 129U, 0U, &byte) == -1);
    CHECK(board_dma_rx_ring_event(&ring, false, true) == -1);
    board_dma_rx_ring_reset(&ring);
    ring.consumed = 127U;
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 0U, &byte) == -1);
    board_dma_rx_ring_reset(&ring);
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 0U, &byte) == 0);
    CHECK(board_dma_rx_ring_pop(NULL, data, 128U, 0U, &byte) == -1);
    CHECK(board_dma_rx_ring_pop(&ring, NULL, 128U, 0U, &byte) == -1);
    CHECK(board_dma_rx_ring_pop(&ring, data, 128U, 0U, NULL) == -1);
    printf("DMA RX ring: %u checks PASS\n", checks);
    return 0;
}
