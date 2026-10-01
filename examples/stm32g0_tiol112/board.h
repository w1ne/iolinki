/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32_TIOL112_BOARD_H
#define STM32_TIOL112_BOARD_H
#include "iolinki/phy_tiol112.h"
#include "rx_queue.h"
void board_init(void);
const iolink_tiol112_io_t* board_tiol112_io(void);
bool board_button(void);
void board_led(bool on);
/* Read-only debugger counters: errors are surfaced to PHY recv as -1 too. */
extern rx_queue_t board_rx;
extern volatile uint32_t board_tx_timeouts;
#endif
