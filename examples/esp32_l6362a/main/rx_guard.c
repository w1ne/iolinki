/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rx_guard.h"
int esp32_rx_run(const esp32_rx_io_t* io, uint8_t* b)
{
    bool bad = io->saturated(io->user);
    int event;
    while ((event = io->event(io->user)) != 0) {
        if (event < 0) bad = true;
    }
    if (bad) {
        io->discard(io->user);
        return -1;
    }
    int result = io->read(io->user, b);
    /* An ISR can publish data and its error after the first queue drain.
     * Check again before the stack receives even a final frame byte. */
    bad = io->saturated(io->user);
    while ((event = io->event(io->user)) != 0) {
        if (event < 0) bad = true;
    }
    if (bad) {
        io->discard(io->user);
        return -1;
    }
    return result;
}
