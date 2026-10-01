/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../src/adapter_state.h"
int main(void)
{
    struct rx_ring r = {0};
    uint8_t b;
    assert(rx_pop(&r, &b) == 0);
    for (unsigned i = 0; i < RX_CAPACITY - 1; i++) assert(rx_push(&r, (uint8_t) i));
    assert(!rx_push(&r, 255));
    assert(r.dropped == 1);
    assert(rx_pop(&r, &b) == -ENOSPC); /* corrupted frame discarded */
    assert(rx_pop(&r, &b) == 0);
    for (unsigned pass = 0; pass < 3; pass++) {
        for (unsigned i = 0; i < 100; i++) assert(rx_push(&r, (uint8_t) i));
        for (unsigned i = 0; i < 100; i++) {
            assert(rx_pop(&r, &b) == 1);
            assert(b == i);
        }
    }
    struct micro_clock c = {.previous = UINT32_MAX - 2, .elapsed = UINT32_MAX - 2};
    assert(micro_extend(&c, UINT32_MAX) == UINT32_MAX);
    assert(micro_extend(&c, 3) == (uint64_t) UINT32_MAX + 4);
    assert(micro_extend(&c, 3) == (uint64_t) UINT32_MAX + 4);
    assert(micro_extend(&c, 1003) == (uint64_t) UINT32_MAX + 1004);
    /* A completed WAKE pulse was latched between IRQ enable and pin sampling. */
    bool latched = true;
    assert(wake_merge_sample(&latched, 0) == 0);
    assert(latched);
    latched = false;
    assert(wake_merge_sample(&latched, 1) == 0);
    assert(latched);
    latched = false;
    assert(wake_merge_sample(&latched, -EIO) == -EIO);
    assert(!latched);
    latched = true;
    assert(wake_merge_sample(&latched, -EIO) == -EIO);
    assert(latched);
    puts("RX ordering, overflow recovery, timer wrap and WAKE latch preservation passed");
}
