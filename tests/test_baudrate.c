
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/phy.h"

/* Mock PHY functions */
static int mock_init(void* user)
{
    (void) user;
    return 0;
}
static void mock_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}
static size_t mock_set_baudrate_call_count;
static iolink_baudrate_t mock_last_baudrate;
static void mock_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    mock_set_baudrate_call_count++;
    mock_last_baudrate = baudrate;
    check_expected(baudrate);
}
static int mock_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    return (int) len;
}
static int mock_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .send = mock_send,
                                          .recv_byte = mock_recv_byte};

static void test_baudrate_invalid_argument_is_noop(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    mock_set_baudrate_call_count = 0U;
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    iolink_dll_init(&ctx, &mock_phy);
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM3), 0);

    const size_t phy_calls_before = mock_set_baudrate_call_count;
    const iolink_baudrate_t phy_rate_before = mock_last_baudrate;
    const iolink_dll_state_t state_before = ctx.state;
    const iolink_baudrate_t baudrate_before = ctx.baudrate;
    const iolink_baudrate_t configured_before = ctx.configured_baudrate;
    const uint32_t t_ren_before = ctx.t_ren_limit_us;
    const uint32_t t_byte_before = ctx.t_byte_limit_us;

    assert_int_equal(iolink_dll_set_baudrate(&ctx, (iolink_baudrate_t) 3), -1);
    assert_int_equal(ctx.state, state_before);
    assert_int_equal(ctx.baudrate, baudrate_before);
    assert_int_equal(ctx.configured_baudrate, configured_before);
    assert_int_equal(ctx.t_ren_limit_us, t_ren_before);
    assert_int_equal(ctx.t_byte_limit_us, t_byte_before);
    assert_int_equal(mock_set_baudrate_call_count, phy_calls_before);
    assert_int_equal(mock_last_baudrate, phy_rate_before);

    assert_int_equal(iolink_dll_set_baudrate(&ctx, (iolink_baudrate_t) -1), -1);
    assert_int_equal(ctx.state, state_before);
    assert_int_equal(ctx.baudrate, baudrate_before);
    assert_int_equal(ctx.configured_baudrate, configured_before);
    assert_int_equal(ctx.t_ren_limit_us, t_ren_before);
    assert_int_equal(ctx.t_byte_limit_us, t_byte_before);
    assert_int_equal(mock_set_baudrate_call_count, phy_calls_before);
    assert_int_equal(mock_last_baudrate, phy_rate_before);
}

static void test_baudrate_init(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* Initialized to COM2 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    iolink_dll_init(&ctx, &mock_phy);

    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM2);
}

static void test_baudrate_switch(void** state)
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
        cmocka_unit_test(test_baudrate_invalid_argument_is_noop),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
