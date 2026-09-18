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

#include "iolinki/device.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_pd_input_output(void** state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* Move to OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* 1. Set Input PD */
    uint8_t input[2] = {0x11, 0x22};
    iolink_device_pd_input_update(&dev.ctx, input, 2, true);

    /* 2. Simulate Master Frame (Type 2_2: MC, CKT, PD_OUT(2), OD(1)) -> 5 bytes.
       Table A.10: TYPE_2_2 carries one OD octet. The CKT carries the Type-2 bits
       (A.1.3). */
    uint8_t frame[5] = {0x80, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Response: PD_IN(2), OD(1), CKS(1) = 4 bytes for TYPE_2_2 (A.1.5) */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);

    iolink_device_process(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_output),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
