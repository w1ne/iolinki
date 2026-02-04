
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/phy.h"

/* Mock PHY functions */
static int mock_init(void)
{
    return 0;
}
static void mock_set_mode(iolink_phy_mode_t mode)
{
    (void) mode;
}
static void mock_set_baudrate(iolink_baudrate_t baudrate)
{
    check_expected(baudrate);
}
static int mock_send(const uint8_t *data, size_t len)
{
    (void) data;
    (void) len;
    return (int) len;
}
static int mock_recv_byte(uint8_t *byte)
{
    (void) byte;
    return 0;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .send = mock_send,
                                          .recv_byte = mock_recv_byte};

static void test_baudrate_init(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* Initialized to COM2 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    iolink_dll_init(&ctx, &mock_phy);

    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM2);
}

static void test_baudrate_switch(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx, &mock_phy);

    /* Switch to COM1 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    int ret = iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM1);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM1);

    /* Switch to COM3 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    ret = iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM3);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM3);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_baudrate_init),
        cmocka_unit_test(test_baudrate_switch),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
