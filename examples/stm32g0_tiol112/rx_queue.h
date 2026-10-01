/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32_RX_QUEUE_H
#define STM32_RX_QUEUE_H
#include <stdint.h>
#define RX_QUEUE_SIZE 128U
typedef struct {
    uint8_t bytes[RX_QUEUE_SIZE];
    volatile uint16_t head, tail;
    volatile uint32_t overflows, errors;
    volatile uint8_t failed;
} rx_queue_t;
void rx_queue_push(rx_queue_t* queue, uint8_t byte);
void rx_queue_error(rx_queue_t* queue);
/* Caller serializes with producer IRQ. Returns -1 once after data loss and
 * discards the damaged buffered frame, 0 empty, 1 byte available. */
int rx_queue_pop(rx_queue_t* queue, uint8_t* byte);
#endif
