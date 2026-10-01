/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef U5_ADAPTER_H
#define U5_ADAPTER_H
#include "iolinki/phy_tiol112.h"
int board_clock_init(void);
extern const iolink_tiol112_io_t board_tiol112_io;
/* Debugger-visible diagnostics; aggregate faults do not identify a cause. */
extern volatile uint32_t board_rx_dropped, board_uart_errors;
extern volatile int board_adapter_error, board_nfault_asserted;
#endif
