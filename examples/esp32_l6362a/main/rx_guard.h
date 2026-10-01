/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESP32_RX_GUARD_H
#define ESP32_RX_GUARD_H
#include <stdbool.h>
#include <stdint.h>
typedef struct
{
    void* user;
    bool (*saturated)(void*);
    /* 1=ordinary event; -1=error event; 0=queue drained. */
    int (*event)(void*);
    void (*discard)(void*);
    int (*read)(void*, uint8_t*);
} esp32_rx_io_t;
int esp32_rx_run(const esp32_rx_io_t*, uint8_t*);
#endif
