/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "main/tx_guard.h"
#include "main/rx_guard.h"
#ifdef NDEBUG
#undef NDEBUG /* Keep behavioral checks active in Release CTest builds. */
#endif
#include <assert.h>
#include <stdio.h>
static uint64_t clock_us;
static bool enabled = true, rx = true, done, timeout, stall;
static unsigned en_attempts, flushes, enqueue_calls;
static bool saturated;
static int events[4], event_pos;
static unsigned discards, reads;
static bool error_during_read, saturation_during_read;
static bool rx_saturated(void* u)
{
    (void) u;
    return saturated;
}
static int rx_event(void* u)
{
    (void) u;
    return events[event_pos++];
}
static void rx_discard(void* u)
{
    (void) u;
    discards++;
}
static int rx_read(void* u, uint8_t* b)
{
    (void) u;
    reads++;
    if (error_during_read) {
        events[event_pos] = -1;
        events[event_pos + 1] = 0;
    }
    if (saturation_during_read) saturated = true;
    *b = 0xa5;
    return 1;
}
static uint64_t now(void* u)
{
    (void) u;
    clock_us += 100;
    return clock_us;
}
static int enqueue(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    (void) b;
    assert(!rx);
    enqueue_calls++;
    return stall ? 0 : (int) n;
}
static int wait_done(void* u, uint32_t ms)
{
    (void) u;
    assert(ms > 0 && enabled && !rx);
    done = !timeout;
    return timeout ? -1 : 0;
}
static void receive_enabled(void* u, bool on)
{
    (void) u;
    if (on) assert(!enabled);
    rx = on;
}
static void enable(void* u, bool on)
{
    (void) u;
    if (on) en_attempts++;
    enabled = on;
}
static void flush(void* u)
{
    (void) u;
    assert(!enabled && !rx);
    flushes++;
}
int main(void)
{
    esp32_tx_guard_t guard = {0};
    esp32_tx_io_t io = {NULL, now, enqueue, wait_done, receive_enabled, enable, flush};
    uint8_t b = 42;
    assert(esp32_tx_run(&guard, &io, &b, 1, 4800) == 1);
    assert(done && !enabled && rx && flushes == 1 && !guard.fault);
    enabled = true;
    timeout = true;
    assert(esp32_tx_run(&guard, &io, &b, 1, 4800) == -1);
    assert(!enabled && rx && guard.fault && !esp32_tx_enable_allowed(&guard));
    unsigned calls = enqueue_calls;
    assert(esp32_tx_run(&guard, &io, &b, 1, 4800) == -1 && enqueue_calls == calls);
    esp32_tx_guard_t fresh = {0};
    clock_us = 0;
    stall = true;
    timeout = false;
    enabled = true;
    assert(esp32_tx_run(&fresh, &io, &b, 1, 230400) == -1);
    assert(fresh.fault && !enabled && clock_us < 10000);
    assert(en_attempts == 0);
    esp32_rx_io_t rx_io = {NULL, rx_saturated, rx_event, rx_discard, rx_read};
    assert(esp32_rx_run(&rx_io, &b) == 1 && b == 0xa5);
    event_pos = 0;
    events[0] = -1;
    events[1] = 1;
    events[2] = 1;
    events[3] = 0;
    assert(esp32_rx_run(&rx_io, &b) == -1 && discards == 1 && reads == 1 && event_pos == 4);
    event_pos = 0;
    events[0] = 0;
    saturated = true;
    assert(esp32_rx_run(&rx_io, &b) == -1 && discards == 2 && reads == 1);
    saturated = false;
    event_pos = 0;
    events[0] = 0;
    error_during_read = true;
    assert(esp32_rx_run(&rx_io, &b) == -1 && discards == 3 && reads == 2);
    error_during_read = false;
    event_pos = 0;
    events[0] = 0;
    events[1] = 0;
    saturation_during_read = true;
    assert(esp32_rx_run(&rx_io, &b) == -1 && discards == 4 && reads == 3);
    puts("ESP32 UART TX ordering/timeouts and RX sticky errors/overflow passed");
}
