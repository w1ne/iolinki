/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32G0_PROCESS_LATCH_H
#define STM32G0_PROCESS_LATCH_H

#include <stdbool.h>
#include <stdint.h>

/* Coalescing wakeup only; the UART RX queue owns the byte history. The
 * foreground consumer must mask producer IRQs around take() so a signal
 * cannot be cleared between its read and reset. */
typedef struct
{
    volatile uint8_t pending;
} board_process_latch_t;

static inline void board_process_latch_signal(board_process_latch_t* latch)
{
    latch->pending = 1U;
}

static inline bool board_process_latch_take(board_process_latch_t* latch)
{
    if (latch->pending == 0U) return false;
    latch->pending = 0U;
    return true;
}

#endif
