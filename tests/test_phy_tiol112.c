/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "iolinki/phy_tiol112.h"

typedef struct
{
    bool enabled, tx_high, fault_high, wake, ready;
    int result, init_result, config_result;
    unsigned baud;
} board_t;
static int init_board(void* p)
{
    ((board_t*) p)->ready = ((board_t*) p)->init_result == 0;
    return ((board_t*) p)->init_result;
}
static void enable(void* p, bool v)
{
    assert_true(((board_t*) p)->ready);
    ((board_t*) p)->enabled = v;
}
static void sio_tx(void* p, bool v)
{
    ((board_t*) p)->tx_high = v;
}
static int configure(void* p, uint32_t baud)
{
    ((board_t*) p)->baud = baud;
    return ((board_t*) p)->config_result;
}
static int transmit(void* p, const uint8_t* b, size_t n)
{
    board_t* board = p;
    assert_true(board->enabled);
    assert_non_null(b);
    return board->result < 0 ? board->result : (int) n;
}
static int receive(void* p, uint8_t* b)
{
    (void) p;
    (void) b;
    return 0;
}
static int consume_wake(void* p)
{
    board_t* b = p;
    int v = b->wake;
    b->wake = false;
    return v;
}
static bool fault_high(void* p)
{
    return ((board_t*) p)->fault_high;
}

static void test_driver_modes_and_turnaround(void** state)
{
    (void) state;
    board_t board = {.fault_high = true};
    const iolink_tiol112_io_t io = {
        .user = &board,
        .init = init_board,
        .set_enable = enable,
        .set_sio_tx = sio_tx,
        .uart_configure = configure,
        .uart_send_complete = transmit,
        .uart_recv = receive,
        .consume_wakeup = consume_wake,
        .read_nfault = fault_high,
    };
    iolink_tiol112_ctx_t driver;
    assert_int_equal(iolink_phy_tiol112_init(&driver, &io), 0);
    const iolink_phy_api_t* phy = iolink_phy_tiol112_get(&driver);
    assert_int_equal(phy->init(phy->user), 0);
    assert_false(board.enabled);
    phy->set_mode(phy->user, IOLINK_PHY_MODE_SIO);
    phy->set_cq_line(phy->user, 1);
    assert_true(board.enabled);
    assert_false(board.tx_high); /* TI truth table: TX low produces CQ high */
    phy->set_cq_line(phy->user, 0);
    assert_true(board.tx_high);
    phy->set_mode(phy->user, IOLINK_PHY_MODE_SDCI);
    assert_false(board.enabled);
    phy->set_baudrate(phy->user, IOLINK_BAUDRATE_COM3);
    assert_int_equal(board.baud, 230400);
    uint8_t data = 0x55;
    assert_int_equal(phy->send(phy->user, &data, 1), 1);
    assert_false(board.enabled);
    board.result = -2;
    assert_int_equal(phy->send(phy->user, &data, 1), -2);
    assert_false(board.enabled);
    board.wake = true;
    assert_int_equal(phy->detect_wakeup(phy->user), 1);
    assert_int_equal(phy->detect_wakeup(phy->user), 0);
    assert_null(phy->is_short_circuit);
    assert_int_equal(iolink_phy_tiol112_fault(&driver), 0);
    board.fault_high = false;
    assert_int_equal(iolink_phy_tiol112_fault(&driver), 1);
    phy->set_mode(phy->user, IOLINK_PHY_MODE_INACTIVE);
    assert_int_equal(phy->send(phy->user, &data, 1), -1);
    board.init_result = -3;
    assert_int_equal(phy->init(phy->user), -3);
    phy->set_mode(phy->user, IOLINK_PHY_MODE_SDCI);
    assert_int_equal(phy->send(phy->user, &data, 1), -1);
    assert_false(board.enabled);
    board.init_result = 0;
    assert_int_equal(phy->init(phy->user), 0);
    board.config_result = -4;
    phy->set_mode(phy->user, IOLINK_PHY_MODE_SDCI);
    assert_int_equal(phy->send(phy->user, &data, 1), -1);
    assert_false(board.enabled);
}

static void test_driver_rejects_incomplete_port(void** state)
{
    (void) state;
    iolink_tiol112_ctx_t driver;
    const iolink_tiol112_io_t io = {0};
    assert_int_equal(iolink_phy_tiol112_init(&driver, &io), -1);
    assert_int_equal(iolink_phy_tiol112_init(NULL, &io), -1);
    assert_null(iolink_phy_tiol112_get(NULL));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_driver_modes_and_turnaround),
        cmocka_unit_test(test_driver_rejects_incomplete_port),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
