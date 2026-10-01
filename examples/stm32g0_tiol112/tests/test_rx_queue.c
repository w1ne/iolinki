#include "rx_queue.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    rx_queue_t q = {0};
    uint8_t byte;
    assert(rx_queue_pop(&q, &byte) == 0);
    for (unsigned round = 0; round < 4; ++round) {
        for (unsigned i = 0; i < RX_QUEUE_SIZE - 1; ++i) rx_queue_push(&q, (uint8_t)i);
        for (unsigned i = 0; i < RX_QUEUE_SIZE - 1; ++i) {
            assert(rx_queue_pop(&q, &byte) == 1);
            assert(byte == i);
        }
    }
    for (unsigned i = 0; i < RX_QUEUE_SIZE; ++i) rx_queue_push(&q, (uint8_t)i);
    assert(q.overflows == 1);
    assert(rx_queue_pop(&q, &byte) == -1);
    assert(rx_queue_pop(&q, &byte) == 0);
    rx_queue_push(&q, 42);
    rx_queue_error(&q);
    assert(q.errors == 1);
    assert(rx_queue_pop(&q, &byte) == -1);
    assert(rx_queue_pop(&q, &byte) == 0);
    rx_queue_push(&q, 7);
    assert(rx_queue_pop(&q, &byte) == 1 && byte == 7);
    puts("RX queue wrap, overflow, UART error and recovery passed");
}
