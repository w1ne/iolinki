/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "adapter.h"
#include "reference_device.h"
#include "iolinki/time_utils.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
static reference_device_t app;
static iolink_tiol112_ctx_t phy;
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
int main(void)
{
    int rc = board_clock_init();
    if (rc) {
        board_adapter_error = rc;
        return rc;
    }
    if (!gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&led)) return -ENODEV;
    if ((rc = gpio_pin_configure_dt(&button, GPIO_INPUT)) ||
        (rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)))
        return rc;
    if ((rc = iolink_phy_tiol112_init(&phy, &board_tiol112_io)) ||
        (rc = reference_device_init(&app, iolink_phy_tiol112_get(&phy)))) {
        board_adapter_error = rc;
        return rc;
    }
    uint64_t next_sample = iolink_time_get_us();
    for (;;) {
        iolink_device_process(&app.device);
        board_nfault_asserted = iolink_phy_tiol112_fault(&phy);
        uint64_t now = iolink_time_get_us();
        if (now >= next_sample) {
            int pressed = gpio_pin_get_dt(&button);
            if (pressed < 0) {
                board_adapter_error = pressed;
                continue;
            }
            rc = reference_device_tick(&app, pressed > 0);
            if (rc) board_adapter_error = rc;
            rc = gpio_pin_set_dt(&led, app.led);
            if (rc) board_adapter_error = rc;
            next_sample = now + 10000U;
        }
        /* No k_sleep: protocol service remains independent of 10 ms sampling. */
    }
}
