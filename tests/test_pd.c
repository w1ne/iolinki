/**
 * @file test_pd.c
 * @brief Unit tests for Process Data exchange
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_pd_cyclic_exchange(void **state)
{
    (void)state;
    
    iolink_init(&g_phy_mock);
    
    /* 1. Move to Preoperate */
    will_return(g_phy_mock.recv_byte, 0x00); 
    will_return(g_phy_mock.recv_byte, 1);
    iolink_process();
    
    /* 2. Move to Operate (via M-Sequence Type 0) */
    will_return(g_phy_mock.recv_byte, 0x00); /* MC */
    will_return(g_phy_mock.recv_byte, 1);
    will_return(g_phy_mock.recv_byte, 0x11); /* CK placeholder */
    will_return(g_phy_mock.recv_byte, 1);
    
    expect_any(g_phy_mock.send, data);
    expect_value(g_phy_mock.send, len, 2);
    will_return(g_phy_mock.send, 2);
    
    will_return(g_phy_mock.recv_byte, 0); /* End of loop */
    iolink_process();

    /* 3. PD Exchange in OPERATE */
    uint8_t test_pd_in = 0xAA;
    iolink_pd_input_update(&test_pd_in, 1, true);
    
    /* Master sends 0x55 (PD output) */
    will_return(g_phy_mock.recv_byte, 0x55);
    will_return(g_phy_mock.recv_byte, 1);
    
    /* Device should respond with 0xAA (PD input) */
    expect_value(g_phy_mock.send, len, 1);
    will_return(g_phy_mock.send, 1);
    
    iolink_process();
    
    /* Verify PD output received */
    uint8_t pd_out_buf[1];
    int read = iolink_pd_output_read(pd_out_buf, 1);
    assert_int_equal(read, 1);
    assert_int_equal(pd_out_buf[0], 0x55);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_cyclic_exchange),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
