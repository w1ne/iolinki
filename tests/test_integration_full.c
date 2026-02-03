/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_integration_full.c
 * @brief Full-stack integration test for iolinki
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include "test_helpers.h"

static void test_full_stack_lifecycle(void **state)
{
    (void)state;
    iolink_phy_mock_reset();
    iolink_ds_mock_reset();
    
    /* Initialize stack with mock PHY and mock storage */
    iolink_init(&g_phy_mock);
    iolink_ds_init(&g_ds_storage_mock);

    /*** STEP 1: STARTUP -> PREOPERATE ***/
    will_return(g_phy_mock.recv_byte, 0x00); /* Wakeup/Sync pulse */
    will_return(g_phy_mock.recv_byte, 1);
    iolink_process();
    /* No state check exported, but we can verify by next responses */

    /*** STEP 2: PREOPERATE (ISDU Read Index 0x10 - Vendor Name) ***/
    /* Master Sends: MC for Index 0x10 Read */
    will_return(g_phy_mock.recv_byte, 0xBB); /* MC: Read Index 0x10 request placeholder */
    will_return(g_phy_mock.recv_byte, 1);
    will_return(g_phy_mock.recv_byte, 0xD4); /* CK for MC 0xBB (Type 0) */
    will_return(g_phy_mock.recv_byte, 1);
    
    /* Device Responds: Expected behavior is returning Vendor Name via multiple cycles */
    expect_any(g_phy_mock.send, buf);
    expect_value(g_phy_mock.send, len, 2);
    iolink_process();

    /*** STEP 3: EVENT TRIGGERING ***/
    iolink_event_trigger(0x1234, IOLINK_EVENT_TYPE_WARNING);
    
    /* Next cycle should have Event bit set in Status/CK */
    will_return(g_phy_mock.recv_byte, 0x00); /* Idle MC */
    will_return(g_phy_mock.recv_byte, 1);
    will_return(g_phy_mock.recv_byte, 0x00); /* CK for Idle */
    will_return(g_phy_mock.recv_byte, 1);
    
    /* Device sends response with Event bit set */
    expect_any(g_phy_mock.send, buf);
    expect_value(g_phy_mock.send, len, 2);
    iolink_process();
    
    /*** STEP 4: DATA STORAGE UPLOAD TRIGGER ***/
    /* Simulate Master asking for DS check by providing 0x0000 checksum (empty master) */
    iolink_ds_check(0x0000);
    iolink_process(); /* Trigger Upload Req */
    iolink_process(); /* Start Uploading */
    iolink_process(); /* Complete Upload */
    
    /* Verify mock storage state or flags? (Functional check) */
    assert_false(iolink_events_pending()); /* Popped at least one event if Master read Index 2? */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_full_stack_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
