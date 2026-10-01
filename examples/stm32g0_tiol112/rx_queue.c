/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rx_queue.h"
void rx_queue_push(rx_queue_t* q, uint8_t byte) {
    uint16_t next = (uint16_t)((q->head + 1U) % RX_QUEUE_SIZE);
    if (next == q->tail) { ++q->overflows; q->failed = 1; return; }
    q->bytes[q->head] = byte;
    q->head = next;
}
void rx_queue_error(rx_queue_t* q) { ++q->errors; q->failed = 1; }
int rx_queue_pop(rx_queue_t* q, uint8_t* byte) {
    if (q->failed) { q->failed = 0; q->tail = q->head; return -1; }
    if (q->tail == q->head) return 0;
    *byte = q->bytes[q->tail];
    q->tail = (uint16_t)((q->tail + 1U) % RX_QUEUE_SIZE);
    return 1;
}
