/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "reference_device.h"
#include "test_helpers.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

static void test_reference_device_payload(void** state)
{
    (void) state;
    reference_device_t app;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(reference_device_init(&app, &g_phy_mock), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&app.device), 3);
    assert_int_equal(iolink_device_get_pd_out_len(&app.device), 1);
    move_to_operate_ctx(&app.device);
    uint8_t frame[5] = {0x80, IOLINK_MSEQ_TYPE_2, 1, 0, 0};
    frame[1] |= iolink_checksum6(frame, sizeof(frame));
    for (size_t i = 0; i < sizeof(frame); i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 6);
    will_return(mock_phy_send, 6);
    iolink_device_process(&app.device);
    assert_int_equal(reference_device_tick(&app, true), 0);
    assert_true(app.led);
    assert_int_equal(app.counter, 1);
    assert_int_equal(app.input[0], 0);
    assert_int_equal(app.input[1], 1);
    assert_int_equal(app.input[2], 1);
    assert_true(iolink_events_pending(iolink_device_get_events_ctx(&app.device)));
    app.counter = 65535;
    assert_int_equal(reference_device_tick(&app, false), 0);
    assert_int_equal(app.counter, 0);
    assert_int_equal(app.input[2], 0);
}

static void test_reference_device_rejects_missing_transport(void** state)
{
    (void) state;
    reference_device_t app;
    iolink_phy_api_t empty = {0};
    assert_int_equal(reference_device_init(NULL, &g_phy_mock), -1);
    assert_int_equal(reference_device_init(&app, NULL), -1);
    assert_int_equal(reference_device_init(&app, &empty), -1);
    assert_int_equal(reference_device_tick(NULL, false), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reference_device_payload),
        cmocka_unit_test(test_reference_device_rejects_missing_transport),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
