/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "reference_device.h"
#include "iolinki/time_utils.h"
static reference_device_t app;
static iolink_tiol112_ctx_t tiol112;
volatile int board_phy_fault;
int main(void)
{
    board_init();
    if (iolink_phy_tiol112_init(&tiol112, board_tiol112_io()) != 0 ||
        reference_device_init(&app, iolink_phy_tiol112_get(&tiol112)) != 0) {
        for (;;) {
        } /* EN remains low on initialization failure */
    }
    uint64_t next_sample = iolink_time_get_us();
    for (;;) {
        iolink_device_process(&app.device);
        uint64_t now = iolink_time_get_us();
        if (now >= next_sample) {
            reference_device_tick(&app, board_button());
            board_led(app.led);
            board_phy_fault = iolink_phy_tiol112_fault(&tiol112);
            next_sample += 10000U; /* 10ms app cadence; never delay protocol RX */
            if (next_sample <= now) next_sample = now + 10000U;
        }
    }
}
