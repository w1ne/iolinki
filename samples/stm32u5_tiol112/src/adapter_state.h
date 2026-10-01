/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ADAPTER_STATE_H
#define ADAPTER_STATE_H
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#define RX_CAPACITY 128U
struct rx_ring
{
    uint8_t bytes[RX_CAPACITY];
    unsigned head, tail;
    bool overflow;
    uint32_t dropped;
};
struct micro_clock
{
    uint32_t previous;
    uint64_t elapsed;
};
static inline bool rx_push(struct rx_ring *r, uint8_t b)
{
    unsigned next = (r->head + 1U) % RX_CAPACITY;
    if (next == r->tail || r->overflow) {
        r->overflow = true;
        r->dropped++;
        return false;
    }
    r->bytes[r->head] = b;
    r->head = next;
    return true;
}
static inline int rx_pop(struct rx_ring *r, uint8_t *b)
{
    if (r->overflow) {
        r->tail = r->head;
        r->overflow = false;
        return -ENOSPC;
    }
    if (r->tail == r->head) return 0;
    *b = r->bytes[r->tail];
    r->tail = (r->tail + 1U) % RX_CAPACITY;
    return 1;
}
/* Call at least once every 2^32 us (71.58 minutes), including during TX. */
static inline uint64_t micro_extend(struct micro_clock *c, uint32_t now)
{
    c->elapsed += (uint32_t) (now - c->previous);
    c->previous = now;
    return c->elapsed;
}
/* Merge the sampled level with an IRQ latch while holding the caller's lock. */
static inline int wake_merge_sample(bool *latched, int active)
{
    if (active < 0) return active;
    *latched = *latched || active > 0;
    return 0;
}
#endif
