/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_tiol112.h"
#include <limits.h>
#include <string.h>

static int initialize(void* user)
{
    iolink_tiol112_ctx_t* d = user;
    if (d->initialized) d->io.set_enable(d->io.user, false);
    d->mode = IOLINK_PHY_MODE_INACTIVE;
    d->error = d->io.init(d->io.user);
    d->initialized = d->error == 0;
    if (d->initialized) d->io.set_enable(d->io.user, false);
    return d->error;
}

static void set_mode(void* user, iolink_phy_mode_t mode)
{
    iolink_tiol112_ctx_t* d = user;
    if (!d->initialized) return;
    d->io.set_enable(d->io.user, false);
    d->mode = mode;
    if (mode == IOLINK_PHY_MODE_SIO) {
        d->io.set_sio_tx(d->io.user, d->cq == 0U);
        if (d->error == 0) d->io.set_enable(d->io.user, true);
    }
    else if (mode == IOLINK_PHY_MODE_SDCI) {
        d->error = d->io.uart_configure(d->io.user, d->baud);
    }
    else if (mode != IOLINK_PHY_MODE_INACTIVE) {
        d->error = -1;
    }
}

static void set_baudrate(void* user, iolink_baudrate_t baud)
{
    iolink_tiol112_ctx_t* d = user;
    switch (baud) {
        case IOLINK_BAUDRATE_COM1:
            d->baud = 4800;
            break;
        case IOLINK_BAUDRATE_COM2:
            d->baud = 38400;
            break;
        case IOLINK_BAUDRATE_COM3:
            d->baud = 230400;
            break;
        default:
            d->error = -1;
            if (d->initialized) d->io.set_enable(d->io.user, false);
            return;
    }
    /* Keep TX in GPIO mode while in SIO. Switch mux only for communication. */
    if (d->initialized && (d->mode == IOLINK_PHY_MODE_SDCI)) {
        d->io.set_enable(d->io.user, false);
        d->error = d->io.uart_configure(d->io.user, d->baud);
    }
}

static int send_data(void* user, const uint8_t* data, size_t len)
{
    iolink_tiol112_ctx_t* d = user;
    if ((d->mode != IOLINK_PHY_MODE_SDCI) || (d->error != 0) || (data == NULL) || (len == 0U) ||
        (len > INT_MAX))
        return -1;
    d->io.set_enable(d->io.user, true);
    int result = d->io.uart_send_complete(d->io.user, data, len);
    /* EN low is the receive state, including partial-write and timeout paths. */
    d->io.set_enable(d->io.user, false);
    return result;
}

static int receive_data(void* user, uint8_t* byte)
{
    iolink_tiol112_ctx_t* d = user;
    if ((d->error != 0) || (byte == NULL)) return -1;
    if (d->mode != IOLINK_PHY_MODE_SDCI) return 0;
    return d->io.uart_recv(d->io.user, byte);
}

static int detect_wakeup(void* user)
{
    iolink_tiol112_ctx_t* d = user;
    if (!d->initialized) return 0;
    return d->io.consume_wakeup(d->io.user);
}

static void set_cq(void* user, uint8_t state)
{
    iolink_tiol112_ctx_t* d = user;
    d->cq = state != 0U ? 1U : 0U;
    if ((d->mode == IOLINK_PHY_MODE_SIO) && (d->error == 0)) {
        /* TI table 8-2: high TX sinks CQ, low TX sources CQ. */
        d->io.set_sio_tx(d->io.user, d->cq == 0U);
        d->io.set_enable(d->io.user, true);
    }
}

static bool fault(void* user)
{
    iolink_tiol112_ctx_t* d = user;
    if (!d->initialized) return true;
    return !d->io.read_nfault(d->io.user);
}

int iolink_phy_tiol112_init(iolink_tiol112_ctx_t* d, const iolink_tiol112_io_t* io)
{
    if ((d == NULL) || (io == NULL) || (io->init == NULL) || (io->set_enable == NULL) ||
        (io->set_sio_tx == NULL) || (io->uart_configure == NULL) ||
        (io->uart_send_complete == NULL) || (io->uart_recv == NULL) || (io->consume_wakeup == NULL))
        return -1;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->baud = 4800;
    d->phy.user = d;
    d->phy.init = initialize;
    d->phy.set_mode = set_mode;
    d->phy.set_baudrate = set_baudrate;
    d->phy.send = send_data;
    d->phy.recv_byte = receive_data;
    d->phy.detect_wakeup = detect_wakeup;
    d->phy.set_cq_line = set_cq;
    if (io->read_nfault != NULL) d->phy.is_short_circuit = fault;
    return 0;
}

const iolink_phy_api_t* iolink_phy_tiol112_get(iolink_tiol112_ctx_t* d)
{
    return d != NULL ? &d->phy : NULL;
}
