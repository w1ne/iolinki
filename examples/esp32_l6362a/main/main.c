/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "reference_device.h"
#include "iolinki/time_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static reference_device_t app;
static iolink_l6362a_ctx_t transceiver;
volatile int board_overload;
volatile int board_phy_fault;
void app_main(void)
{
    if (iolink_phy_l6362a_init(&transceiver, board_io()) != 0 ||
        reference_device_init(&app, iolink_phy_l6362a_get(&transceiver)) != 0) {
        for (;;) vTaskDelay(portMAX_DELAY);
    }
    uint64_t next_sample = iolink_time_get_us(), next_yield = next_sample + 20000U;
    for (;;) {
        iolink_device_process(&app.device);
        uint64_t now = iolink_time_get_us();
        if (now >= next_sample) {
            reference_device_tick(&app, board_button());
            board_led(app.led);
            board_overload = iolink_phy_l6362a_overload(&transceiver);
            board_phy_fault = iolink_phy_l6362a_fault(&transceiver);
            next_sample = now + 10000U;
        }
        /* Wake is IRQ-latched in SIO. Never insert a 1ms sleep into SDCI:
         * the single C3 core is reserved for protocol processing during a link. */
        if (now >= next_yield) {
            if (transceiver.mode != IOLINK_PHY_MODE_SDCI)
                vTaskDelay(1);
            else
                taskYIELD();
            next_yield = now + 20000U;
        }
    }
}
