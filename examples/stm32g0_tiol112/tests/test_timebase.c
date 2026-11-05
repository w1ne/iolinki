/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "timebase.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check_equal(uint32_t actual, uint32_t expected, int line)
{
    ++checks;
    if (actual == expected) return;
    fprintf(stderr, "Timebase check failed at %d: %u != %u\n", line, actual, expected);
    exit(1);
}
#define EQUAL(actual, expected) check_equal((actual), (expected), __LINE__)

static uint32_t reference_ms(uint32_t high, uint32_t low)
{
    return (uint32_t) ((((uint64_t) high << 32U) | low) / 1000U);
}

int main(void)
{
    static const uint32_t lows[] = {
        0U,    1U,    295U,        296U,        703U,        704U,        999U,
        1000U, 1001U, 4294966999U, 4294967000U, 0xfffffffeU, 0xffffffffU,
    };
    uint32_t epoch_ms = 0U;
    uint32_t remainder = 0U;
    /* 5000 timer wraps include 40 fractional-carry periods and five complete
     * uint32 millisecond rollovers. The reference uses independent 64-bit math. */
    for (uint32_t high = 0U; high <= 5000U; ++high) {
        for (unsigned i = 0U; i < sizeof(lows) / sizeof(lows[0]); ++i) {
            EQUAL(board_time_milliseconds(epoch_ms, remainder, lows[i]),
                  reference_ms(high, lows[i]));
        }
        uint32_t pending_ms = epoch_ms;
        uint32_t pending_remainder = remainder;
        board_time_advance_epoch(&pending_ms, &pending_remainder, high + 1U);
        EQUAL(board_time_milliseconds(pending_ms, pending_remainder, 17U),
              reference_ms(high + 1U, 17U));
        board_time_advance_epoch(&epoch_ms, &remainder, high + 1U);
        EQUAL(epoch_ms, pending_ms); /* Pending-UIF reader and later ISR agree. */
        EQUAL(remainder, pending_remainder);
    }

    static const uint32_t highs[] = {
        124U, 125U, 999U, 1000U, 0x7fffffffU, 0xfffffffeU, 0xffffffffU,
    };
    for (unsigned i = 0U; i < sizeof(highs) / sizeof(highs[0]); ++i) {
        uint64_t prefix = (uint64_t) highs[i] << 32U;
        epoch_ms = (uint32_t) (prefix / 1000U);
        remainder = (uint32_t) (prefix % 1000U);
        for (unsigned j = 0U; j < sizeof(lows) / sizeof(lows[0]); ++j) {
            EQUAL(board_time_milliseconds(epoch_ms, remainder, lows[j]),
                  reference_ms(highs[i], lows[j]));
        }
        board_time_advance_epoch(&epoch_ms, &remainder, highs[i] + 1U);
        EQUAL(board_time_milliseconds(epoch_ms, remainder, 37U), reference_ms(highs[i] + 1U, 37U));
    }
    printf("TIM2 millisecond timebase: %u exact-reference checks PASS\n", checks);
    return 0;
}
