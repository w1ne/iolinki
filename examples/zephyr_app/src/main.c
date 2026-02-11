/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Copyright (c) 2026 iolinki-project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "iolinki/iolink.h"

#ifdef CONFIG_IOLINK_PHY_UART
#include "platform/zephyr/phy_uart.h"
#else
#include "iolinki/phy_virtual.h"
#endif


#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(iolink_demo, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Starting IO-Link Zephyr Demo");

    const char *port = NULL; 

#ifdef CONFIG_IOLINK_PHY_VIRTUAL
    port = getenv("IOLINK_PORT");
    if (port) {
        iolink_phy_virtual_set_port(port);
        LOG_INF("Connecting to %s", port);
    }
    else {
        LOG_WRN("IOLINK_PORT not set, using default");
    }
#endif


    /* Prepare configuration from environment */
    iolink_config_t config;
    memset(&config, 0, sizeof(config));

    /* Set defaults */
    config.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.pd_in_len = 2;  /* Default */
    config.pd_out_len = 2; /* Default */

    const char *m_seq_env = getenv("IOLINK_M_SEQ_TYPE");
    if (m_seq_env) {
        config.m_seq_type = (iolink_m_seq_type_t) atoi(m_seq_env);
        LOG_INF("Configured M-Sequence Type: %d", config.m_seq_type);
    }

    const char *pd_len_env = getenv("IOLINK_PD_LEN");
    if (pd_len_env) {
        int len = atoi(pd_len_env);
        config.pd_in_len = (uint8_t) len;
        config.pd_out_len = (uint8_t) len;
        LOG_INF("Configured PD Length: %d", len);
    }

    /* Initialize PHY */
    const iolink_phy_api_t *phy_api = NULL;

#ifdef CONFIG_IOLINK_PHY_UART
    const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(iolink_uart));
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }
    if (iolink_phy_uart_init(uart_dev) != 0) {
        LOG_ERR("Failed to init UART PHY");
        return -1;
    }
    phy_api = iolink_phy_uart_get();
    LOG_INF("Using UART PHY with device: %s", uart_dev->name);
#else
    phy_api = iolink_phy_virtual_get();
    LOG_INF("Using Virtual PHY");
#endif

#ifdef CONFIG_IOLINK_DEMO_MASTER
    LOG_INF("Running as DEMO MASTER");

    while (1) {
        /* Send Master Command: Read Direct Parameter 1 (Index 0) */
        /* Send Master Command: Read Direct Parameter 1 (Index 0) */
        /* MC = 0x80, CKT = 0x00, CK = 0x24 */
        uint8_t frame[] = { 0x80, 0x24 }; 
        phy_api->send(frame, 2);
        
        uint8_t rx;
        while (phy_api->recv_byte(&rx) > 0) {
            LOG_INF("Master RX: 0x%02X", rx);
        }
        
        k_msleep(2000);
    }
#else
    if (iolink_init(phy_api, &config) != 0) {
        LOG_ERR("Failed to init IO-Link");
        return -1;
    }

    uint8_t sensor_val = 0;
    uint32_t last_update = 0;

    while (1) {
        iolink_process();
        
        /* Simulating sensor data change every 2000ms */
        uint32_t now = k_uptime_get_32();
        if (now - last_update > 2000) {
            last_update = now;
            sensor_val++;
            
            uint8_t pd[2] = { sensor_val, 0xA5 };
            iolink_pd_input_update(pd, 2, true);
            
            LOG_INF("Device PD Update: 0x%02X", sensor_val);
        }
        
        k_msleep(1); 
    }
#endif
    return 0;
}
