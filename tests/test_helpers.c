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

/* Mock PHY operations */
mock_phy_ops_t g_mock_phy;

static int mock_phy_send_byte(uint8_t byte)
{
    (void)byte;
    return (int)mock();
}

static int mock_phy_recv_byte(uint8_t *byte)
{
    *byte = (uint8_t)mock();
    return (int)mock();
}

static void mock_phy_set_mode(int mode)
{
    check_expected(mode);
}

void setup_mock_phy(void)
{
    memset(&g_mock_phy, 0, sizeof(g_mock_phy));
    g_mock_phy.send_byte = mock_phy_send_byte;
    g_mock_phy.recv_byte = mock_phy_recv_byte;
    g_mock_phy.set_mode = mock_phy_set_mode;
}

uint32_t mock_get_time_ms(void)
{
    return (uint32_t)mock();
}

void mock_delay_ms(uint32_t ms)
{
    check_expected(ms);
}
