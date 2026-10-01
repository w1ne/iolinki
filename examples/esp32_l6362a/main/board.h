/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESP32_REFERENCE_BOARD_H
#define ESP32_REFERENCE_BOARD_H
#include "iolinki/phy_l6362a.h"
/* ESP32-C3-DevKitM-1 (ESP32-C3-MINI-1), external LED and button. */
#define PIN_TX 4
#define PIN_RX 5
#define PIN_EN 6
#define PIN_OL 7
#define PIN_DIAG 10
#define PIN_BUTTON 0
#define PIN_LED 1
const iolink_l6362a_io_t* board_io(void);
bool board_button(void);
void board_led(bool on);
#endif
