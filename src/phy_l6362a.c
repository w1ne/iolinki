/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_l6362a.h"
#include <limits.h>
#include <string.h>
static void enable(iolink_l6362a_ctx_t* d, bool on)
{
    d->io.set_enable(d->io.user, on);
    d->enabled = on;
}
static int initialize(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    if (d->initialized) enable(d, false);
    d->error = d->io.init(d->io.user);
    d->initialized = d->error == 0;
    d->mode = IOLINK_PHY_MODE_INACTIVE;
    if (d->initialized) enable(d, false);
    return d->error;
}
static void mode(void* u, iolink_phy_mode_t m)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized) return;
    enable(d, false);
    d->mode = m;
    if (m == IOLINK_PHY_MODE_SIO) {
        d->io.set_in2(d->io.user, d->cq);
        if (d->error == 0) enable(d, true);
    }
    else if (m == IOLINK_PHY_MODE_SDCI)
        d->error = d->io.uart_configure(d->io.user, d->baud);
    else if (m != IOLINK_PHY_MODE_INACTIVE)
        d->error = -1;
}
static void speed(void* u, iolink_baudrate_t b)
{
    iolink_l6362a_ctx_t* d = u;
    switch (b) {
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
            if (d->initialized) enable(d, false);
            return;
    }
    if (d->initialized && d->mode == IOLINK_PHY_MODE_SDCI) {
        enable(d, false);
        d->error = d->io.uart_configure(d->io.user, d->baud);
    }
}
static int send_data(void* u, const uint8_t* bytes, size_t n)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized || d->mode != IOLINK_PHY_MODE_SDCI || d->error || !bytes || !n ||
        n > INT_MAX)
        return -1;
    enable(d, true);
    int r = d->io.uart_send_complete(d->io.user, bytes, n);
    enable(d, false);
    return r;
}
static int receive(void* u, uint8_t* b)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized || d->error || !b) return -1;
    return d->mode == IOLINK_PHY_MODE_SDCI ? d->io.uart_recv(d->io.user, b) : 0;
}
static int wake(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    return d->initialized ? d->io.consume_wakeup(d->io.user) : 0;
}
static void cq(void* u, uint8_t b)
{
    iolink_l6362a_ctx_t* d = u;
    d->cq = b != 0;
    if (d->initialized && !d->error && d->mode == IOLINK_PHY_MODE_SIO) {
        /* IN1=GND: table 14 CQ=IN2. IN2 transitions get dead-time protection. */
        d->io.set_in2(d->io.user, d->cq);
        enable(d, true);
    }
}
int iolink_phy_l6362a_init(iolink_l6362a_ctx_t* d, const iolink_l6362a_io_t* io)
{
    if (!d || !io || !io->init || !io->set_enable || !io->set_in2 || !io->uart_configure ||
        !io->uart_send_complete || !io->uart_recv || !io->consume_wakeup)
        return -1;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->baud = 4800;
    d->phy.user = d;
    d->phy.init = initialize;
    d->phy.set_mode = mode;
    d->phy.set_baudrate = speed;
    d->phy.send = send_data;
    d->phy.recv_byte = receive;
    d->phy.detect_wakeup = wake;
    d->phy.set_cq_line = cq;
    return 0;
}
const iolink_phy_api_t* iolink_phy_l6362a_get(iolink_l6362a_ctx_t* d)
{
    return d ? &d->phy : NULL;
}
int iolink_phy_l6362a_fault(const iolink_l6362a_ctx_t* d)
{
    if (!d || !d->initialized || !d->enabled || !d->io.read_diag) return -1;
    return d->io.read_diag(d->io.user) ? 0 : 1;
}
int iolink_phy_l6362a_overload(const iolink_l6362a_ctx_t* d)
{
    if (!d || !d->initialized || !d->io.read_ol) return -1;
    return d->io.read_ol(d->io.user) ? 0 : 1;
}
