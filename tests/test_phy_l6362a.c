/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_l6362a.h"
#ifdef NDEBUG
#undef NDEBUG /* Keep behavioral checks active in Release CTest builds. */
#endif
#include <assert.h>
#include <stdio.h>
static bool en, in2, ol = true, diag = true;
static int tx_result = 1, config_result;
static uint32_t baud;
static int start(void* u)
{
    (void) u;
    en = false;
    return 0;
}
static void enable(void* u, bool b)
{
    (void) u;
    en = b;
}
static void level(void* u, bool b)
{
    (void) u;
    in2 = b;
}
static int config(void* u, uint32_t b)
{
    (void) u;
    assert(!en);
    baud = b;
    return config_result;
}
static int send_bytes(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    (void) b;
    (void) n;
    assert(en);
    return tx_result;
}
static int recv_byte(void* u, uint8_t* b)
{
    (void) u;
    *b = 0xa5;
    return 1;
}
static int wake(void* u)
{
    (void) u;
    return 1;
}
static bool read_ol(void* u)
{
    (void) u;
    return ol;
}
static bool read_diag(void* u)
{
    (void) u;
    return diag;
}
int main(void)
{
    iolink_l6362a_ctx_t d;
    iolink_l6362a_io_t io = {NULL,       start,     enable, level,   config,
                             send_bytes, recv_byte, wake,   read_ol, read_diag};
    assert(iolink_phy_l6362a_init(&d, &io) == 0);
    const iolink_phy_api_t* p = iolink_phy_l6362a_get(&d);
    assert(p->init(p->user) == 0 && !en);
    p->set_cq_line(p->user, 1);
    p->set_mode(p->user, IOLINK_PHY_MODE_SIO);
    assert(en && in2); /* L6362A IN1=0 is noninverting, unlike TIOL112. */
    diag = false;
    assert(iolink_phy_l6362a_fault(&d) == 1);
    diag = true;
    p->set_cq_line(p->user, 0);
    assert(en && !in2);
    p->set_mode(p->user, IOLINK_PHY_MODE_SDCI);
    assert(!en && baud == 4800);
    assert(iolink_phy_l6362a_fault(&d) == -1);
    p->set_baudrate(p->user, IOLINK_BAUDRATE_COM3);
    assert(baud == 230400);
    uint8_t b = 1;
    assert(p->send(p->user, &b, 1) == 1 && !en);
    tx_result = -1;
    assert(p->send(p->user, &b, 1) == -1 && !en);
    assert(p->recv_byte(p->user, &b) == 1 && b == 0xa5);
    ol = false;
    assert(iolink_phy_l6362a_overload(&d) == 1);
    assert(p->detect_wakeup(p->user) == 1);
    config_result = -1;
    p->set_mode(p->user, IOLINK_PHY_MODE_SDCI);
    assert(!en && p->send(p->user, &b, 1) == -1);
    p->set_mode(p->user, IOLINK_PHY_MODE_INACTIVE);
    assert(!en);
    assert(iolink_phy_l6362a_init(NULL, &io) == -1);
    puts("L6362A polarity, receive state, errors and diagnostics passed");
}
