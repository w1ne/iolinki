/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "process_latch.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    board_process_latch_t latch = {0};

    assert(!board_process_latch_take(&latch));
    board_process_latch_signal(&latch);
    assert(board_process_latch_take(&latch));
    assert(!board_process_latch_take(&latch));

    /* Several IRQs before foreground service coalesce into one wake; queued RX
     * bytes remain distinct in rx_queue and are drained by that service. */
    board_process_latch_signal(&latch);
    board_process_latch_signal(&latch);
    assert(board_process_latch_take(&latch));
    assert(!board_process_latch_take(&latch));

    /* An event arriving just after a take remains pending for the next pass. */
    board_process_latch_signal(&latch);
    assert(board_process_latch_take(&latch));
    board_process_latch_signal(&latch);
    assert(board_process_latch_take(&latch));
    assert(!board_process_latch_take(&latch));

    puts("Foreground process event latch coalescing and handoff passed");
    return 0;
}
