/**
 * @file test_helpers.c
 * @brief Shared test utilities and mock implementations
 */

#include "test_helpers.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

/* Test buffers */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* Mock implementations */

static int mock_phy_init(void)
{
    return (int)mock();
}

static void mock_phy_set_mode(iolink_phy_mode_t mode)
{
    check_expected(mode);
}

static void mock_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    check_expected(baudrate);
}

static int mock_phy_send(const uint8_t *data, size_t len)
{
    check_expected_ptr(data);
    check_expected(len);
    return (int)mock();
}

static int mock_phy_recv_byte(uint8_t *byte)
{
    check_expected_ptr(byte);
    *byte = (uint8_t)mock();
    return (int)mock();
}

const iolink_phy_api_t g_phy_mock = {
    .init = mock_phy_init,
    .set_mode = mock_phy_set_mode,
    .set_baudrate = mock_phy_set_baudrate,
    .send = mock_phy_send,
    .recv_byte = mock_phy_recv_byte
};
