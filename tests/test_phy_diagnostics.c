/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_phy_diagnostics.c
 * @brief Unit tests for PHY diagnostics (voltage monitoring and short circuit detection)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "iolinki/dll.h"
#include "iolinki/phy.h"
#include "iolinki/events.h"

/* Mock PHY implementation with configurable voltage and fault returns */
static int mock_voltage_mv = 24000; /* Default: 24V (normal) */
static bool mock_short_circuit = false;

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
    (void) baudrate;
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
    return 0; /* No data */
}

static int mock_get_voltage_mv(void)
{
    return mock_voltage_mv;
}

static bool mock_is_short_circuit(void)
{
    return mock_short_circuit;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .send = mock_send,
                                          .recv_byte = mock_recv_byte,
                                          .detect_wakeup = NULL,
                                          .set_cq_line = NULL,
                                          .get_voltage_mv = mock_get_voltage_mv,
                                          .is_short_circuit = mock_is_short_circuit};

/* PHY without diagnostics support */
static const iolink_phy_api_t mock_phy_no_diag = {.init = mock_init,
                                                  .set_mode = mock_set_mode,
                                                  .set_baudrate = mock_set_baudrate,
                                                  .send = mock_send,
                                                  .recv_byte = mock_recv_byte,
                                                  .detect_wakeup = NULL,
                                                  .set_cq_line = NULL,
                                                  .get_voltage_mv = NULL,
                                                  .is_short_circuit = NULL};

static void test_voltage_monitoring_normal(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* 24V - normal */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* No voltage faults should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_voltage_monitoring_low(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 12000; /* 12V - too low */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Voltage fault should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_PHY_VOLTAGE_FAULT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_voltage_monitoring_high(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 35000; /* 35V - too high */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Voltage fault should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_PHY_VOLTAGE_FAULT);
}

static void test_voltage_monitoring_multiple_cycles(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* 10V - too low */

    iolink_dll_init(&ctx, &mock_phy);

    /* Run multiple process cycles */
    for (int i = 0; i < 5; i++) {
        iolink_dll_process(&ctx);
    }

    /* Voltage faults should accumulate */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 5);
}

static void test_short_circuit_detection(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* Ensure normal voltage */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Short circuit should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 1);

    /* Event should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_PHY_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
}

static void test_short_circuit_no_fault(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* Ensure normal voltage */
    mock_short_circuit = false;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* No short circuit should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_phy_no_diagnostics_support(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* Use PHY without diagnostics support */
    iolink_dll_init(&ctx, &mock_phy_no_diag);
    iolink_dll_process(&ctx);

    /* Should not crash, counters should remain zero */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_int_equal(stats.short_circuits, 0);
}

static void test_combined_faults(void **state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* Too low */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* Both faults should be detected */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(stats.short_circuits, 1);

    /* Both events should be triggered */
    assert_true(iolink_events_pending(&ctx.events));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_voltage_monitoring_normal),
        cmocka_unit_test(test_voltage_monitoring_low),
        cmocka_unit_test(test_voltage_monitoring_high),
        cmocka_unit_test(test_voltage_monitoring_multiple_cycles),
        cmocka_unit_test(test_short_circuit_detection),
        cmocka_unit_test(test_short_circuit_no_fault),
        cmocka_unit_test(test_phy_no_diagnostics_support),
        cmocka_unit_test(test_combined_faults),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
