/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 *
 * Minimal IO-Link device sample.
 *
 * The device:
 *   - selects a PHY (UART on real hardware, Virtual on native_sim),
 *   - initializes the stack with a small fixed-PD configuration,
 *   - registers application lifecycle and process-data callbacks,
 *   - publishes a 2-byte input process-data word every cycle,
 *   - services the stack from the main loop via iolink_process().
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "iolinki/iolink.h"

#ifdef CONFIG_IOLINK_PHY_UART
#include "platform/zephyr/phy_uart.h"
#else
#include "iolinki/phy_virtual.h"
#endif

LOG_MODULE_REGISTER(iolink_device_sample, LOG_LEVEL_INF);

#define SAMPLE_PD_IN_LEN 2
#define SAMPLE_PD_OUT_LEN 2

static void on_operate(void)
{
    LOG_INF("IO-Link link entered OPERATE");
}

static void on_pd_output(uint8_t* data, uint8_t len)
{
    /* Process data received from the Master (Master -> Device). */
    if (len >= 1) {
        LOG_DBG("PD out from Master: 0x%02X", data[0]);
    }
}

static const iolink_app_callbacks_t app_callbacks = {
    .on_operate = on_operate,
    .on_pd_output = on_pd_output,
};

int main(void)
{
    LOG_INF("Starting iolinki IO-Link device sample");

    const iolink_phy_api_t* phy = NULL;

#ifdef CONFIG_IOLINK_PHY_UART
    if (iolink_phy_uart_init_default() != 0) {
        LOG_ERR("UART PHY init failed (check 'zephyr,iolink-uart' chosen node)");
        return -1;
    }
    phy = iolink_phy_uart_get();
    LOG_INF("Using UART PHY");
#else
    phy = iolink_phy_virtual_get();
    LOG_INF("Using Virtual PHY");
#endif

    iolink_config_t config;
    memset(&config, 0, sizeof(config));
    config.m_seq_type = (iolink_m_seq_type_t) CONFIG_IOLINK_M_SEQ_DEFAULT_TYPE;
    config.min_cycle_time = 20; /* 2.0 ms */
    config.pd_in_len = SAMPLE_PD_IN_LEN;
    config.pd_out_len = SAMPLE_PD_OUT_LEN;
    config.t_pd_us = 0;

    if (iolink_init(phy, &config) != 0) {
        LOG_ERR("iolink_init failed");
        return -1;
    }

    iolink_app_register(&app_callbacks);

    uint8_t counter = 0;
    uint32_t last_update = 0;

    while (1) {
        iolink_process();

        uint32_t now = k_uptime_get_32();
        if (now - last_update >= 1000) {
            last_update = now;
            counter++;

            const uint8_t pd_in[SAMPLE_PD_IN_LEN] = {counter, 0xA5};
            iolink_pd_input_update(pd_in, sizeof(pd_in), true);
            LOG_INF("Published PD in: 0x%02X 0x%02X", pd_in[0], pd_in[1]);
        }

        k_msleep(1);
    }

    return 0;
}
