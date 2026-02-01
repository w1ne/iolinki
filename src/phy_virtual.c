#include "iolinki/phy_virtual.h"
#include <stdio.h>

static int virtual_init(void)
{
    printf("[PHY-VIRTUAL] Initialized connection to simulation server\n");
    return 0;
}

static void virtual_set_mode(iolink_phy_mode_t mode)
{
    printf("[PHY-VIRTUAL] Mode set to: %d\n", mode);
}

static void virtual_set_baudrate(iolink_baudrate_t baudrate)
{
    printf("[PHY-VIRTUAL] Baudrate set to: %d\n", baudrate);
}

static int virtual_send(const uint8_t *data, size_t len)
{
    (void)data;
    printf("[PHY-VIRTUAL] Sending %zu bytes\n", len);
    return (int)len;
}

static int virtual_recv_byte(uint8_t *byte)
{
    (void)byte;
    /* Placeholder for socket receive logic */
    return 0;
}

static const iolink_phy_api_t g_phy_virtual = {
    .init = virtual_init,
    .set_mode = virtual_set_mode,
    .set_baudrate = virtual_set_baudrate,
    .send = virtual_send,
    .recv_byte = virtual_recv_byte
};

const iolink_phy_api_t* iolink_phy_virtual_get(void)
{
    return &g_phy_virtual;
}
