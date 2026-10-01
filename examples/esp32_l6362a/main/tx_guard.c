/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tx_guard.h"
bool esp32_tx_enable_allowed(const esp32_tx_guard_t* s)
{
    return !s->fault;
}
int esp32_tx_run(esp32_tx_guard_t* s, const esp32_tx_io_t* io, const uint8_t* b, size_t n,
                 uint32_t baud)
{
    if (s->fault || !b || !n || n > 256 || !baud) return -1;
    uint32_t budget_ms = (uint32_t) (((uint64_t) n * 11000U + baud - 1U) / baud) + 3U;
    uint64_t deadline = io->now(io->user) + (uint64_t) budget_ms * 1000U;
    io->receive_enabled(io->user, false);
    size_t sent = 0;
    while (sent < n && io->now(io->user) < deadline) {
        int count = io->enqueue(io->user, b + sent, n - sent);
        if (count < 0 || (size_t) count > n - sent) break;
        sent += (size_t) count;
    }
    int result = -1;
    uint64_t current = io->now(io->user);
    if (sent == n && current < deadline &&
        io->wait_done(io->user, (uint32_t) ((deadline - current + 999U) / 1000U)) == 0)
        result = (int) n;
    if (result < 0) s->fault = true;
    /* Stop output first, before echo cleanup and RX re-arm. On timeout pending
     * UART bytes may still exist, so latch fault until reboot: never re-enable. */
    io->enable(io->user, false);
    io->flush_echo(io->user);
    io->receive_enabled(io->user, true);
    return result;
}
