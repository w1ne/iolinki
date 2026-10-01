/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PHY_L6362A_H
#define IOLINK_PHY_L6362A_H
#include "iolinki/phy.h"
/* IN1 must be hardwired low: CQ=IN2, RX=OUTI/Q (ST tables 14/15).
 * EN must drive through the board series resistor; DIAG samples its IC side.
 * init leaves EN low. uart_send_complete suppresses echo and waits for final
 * stop bit with a bounded deadline. consume_wakeup consumes an OL falling IRQ.
 * OL is overload/current limitation, not a dedicated wake output. */
typedef struct
{
    void* user;
    int (*init)(void*);
    void (*set_enable)(void*, bool);
    void (*set_in2)(void*, bool);
    int (*uart_configure)(void*, uint32_t);
    int (*uart_send_complete)(void*, const uint8_t*, size_t);
    int (*uart_recv)(void*, uint8_t*);
    int (*consume_wakeup)(void*);
    bool (*read_ol)(void*);
    bool (*read_diag)(void*);
} iolink_l6362a_io_t;
typedef struct
{
    iolink_phy_api_t phy;
    iolink_l6362a_io_t io;
    iolink_phy_mode_t mode;
    uint32_t baud;
    int error;
    bool initialized, enabled, cq;
} iolink_l6362a_ctx_t;
int iolink_phy_l6362a_init(iolink_l6362a_ctx_t*, const iolink_l6362a_io_t*);
const iolink_phy_api_t* iolink_phy_l6362a_get(iolink_l6362a_ctx_t*);
/* 1=overload asserted; 0=clear; -1=unavailable. No short/thermal cause inferred. */
int iolink_phy_l6362a_overload(const iolink_l6362a_ctx_t*);
/* DIAG fault only meaningful while EN requested high; -1 otherwise.
 * Asserted aggregates cut-off/UVLO/overtemperature; cannot identify cause. */
int iolink_phy_l6362a_fault(const iolink_l6362a_ctx_t*);
#endif
