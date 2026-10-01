/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PHY_TIOL112_H
#define IOLINK_PHY_TIOL112_H
#include "iolinki/phy.h"

/* MCU adapter contract. No pin numbers, OS, compiler extensions or global state.
 * init configures EN low and pull-ups/IRQ for active-low WAKE and NFAULT.
 * On failure it must leave EN low; other callbacks need only work after success.
 * uart_configure selects the UART pin mux, 8 data bits, even parity, 1 stop bit.
 * uart_send_complete must return only AFTER the final stop bit leaves the pin,
 * with local RX echo suppressed and a bounded timeout. Return bytes or negative.
 * set_sio_tx selects GPIO mux and sets the raw TIOL112 TX pin level.
 * consume_wakeup atomically consumes an IRQ-latched active-low WAKE event.
 * read_nfault returns the raw pin level (true = no fault); optional. */
typedef struct
{
    void* user;
    int (*init)(void* user);
    void (*set_enable)(void* user, bool enabled);
    void (*set_sio_tx)(void* user, bool high);
    int (*uart_configure)(void* user, uint32_t baud);
    int (*uart_send_complete)(void* user, const uint8_t* data, size_t len);
    int (*uart_recv)(void* user, uint8_t* byte);
    int (*consume_wakeup)(void* user);
    bool (*read_nfault)(void* user);
} iolink_tiol112_io_t;

typedef struct
{
    iolink_phy_api_t phy;
    iolink_tiol112_io_t io;
    iolink_phy_mode_t mode;
    uint32_t baud;
    uint8_t cq;
    int error;
    bool initialized;
} iolink_tiol112_ctx_t;

/* Copies IO callbacks; driver and io.user must outlive the device. */
int iolink_phy_tiol112_init(iolink_tiol112_ctx_t* driver, const iolink_tiol112_io_t* io);
const iolink_phy_api_t* iolink_phy_tiol112_get(iolink_tiol112_ctx_t* driver);
/* Aggregate NFAULT: undervoltage, temperature or short circuit. No cause inferred.
 * Returns 1 when asserted, 0 clear, -1 if uninitialized or pin unavailable. */
int iolink_phy_tiol112_fault(const iolink_tiol112_ctx_t* driver);
#endif
