/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd.c
 * @brief Unit tests for Process Data (PD) handling
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_pd_input_output(void **state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    /* Move to OPERATE */
    move_to_operate();

    /* 1. Set Input PD */
    uint8_t input[2] = {0x11, 0x22};
    iolink_pd_input_update(input, 2, true);

    /* 2. Simulate Master Frame (Type 2_2: MC, CKT, PD_OUT(2), OD(2), CK) -> 7 bytes */
    uint8_t frame[7] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    frame[6] = iolink_crc6(frame, 6);

    for (int i = 0; i < 7; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Response: Status(1), PD_IN(2), OD(2), CK(1) = 6 bytes for Type 2_x */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 6);
    will_return(mock_phy_send, 0);

    iolink_process();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_output),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
