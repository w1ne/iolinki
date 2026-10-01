/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESP32_TX_GUARD_H
#define ESP32_TX_GUARD_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct
{
    void* user;
    uint64_t (*now)(void*);
    int (*enqueue)(void*, const uint8_t*, size_t);
    int (*wait_done)(void*, uint32_t);
    void (*receive_enabled)(void*, bool);
    void (*enable)(void*, bool);
    void (*flush_echo)(void*);
} esp32_tx_io_t;
typedef struct
{
    bool fault;
} esp32_tx_guard_t;
int esp32_tx_run(esp32_tx_guard_t*, const esp32_tx_io_t*, const uint8_t*, size_t, uint32_t);
bool esp32_tx_enable_allowed(const esp32_tx_guard_t*);
#endif
