/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (C) 2026 Andrii Shylenko
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_events_dll.c
 * @brief The A.6.4 lifecycle is serviced by iolink_dll_process().
 *
 * An application that only pumps the DLL must still get its deferred
 * "event disappears" without calling iolink_events_process() itself.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"

static uint32_t g_now_ms;

uint32_t iolink_time_get_ms(void)
{
    return g_now_ms;
}

uint64_t iolink_time_get_us(void)
{
    return (uint64_t) g_now_ms * 1000ULL;
}

static int phy_init(void* user)
{
    (void) user;
    return 0;
}

static void phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static int phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    return (int) len;
}

static int phy_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

int main(void)
{
    const iolink_phy_api_t phy = {
        .user = NULL,
        .init = phy_init,
        .set_mode = phy_set_mode,
        .set_baudrate = phy_set_baudrate,
        .send = phy_send,
        .recv_byte = phy_recv_byte,
    };

    iolink_dll_ctx_t ctx;
    g_now_ms = 0U;
    iolink_dll_init(&ctx, &phy);

    iolink_event_trigger(&ctx.events, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx.events, IOLINK_EVENTCODE_SHORT_CIRCUIT);

    /* Only the DLL is pumped, never iolink_events_process(). */
    iolink_dll_process(&ctx);
    iolink_event_t ev;
    assert(iolink_events_pop(&ctx.events, &ev));
    assert(ev.code == IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert(ev.mode == IOLINK_EVENT_MODE_APPEARS);
    assert(!iolink_events_pop(&ctx.events, &ev)); /* 50 ms window still open */

    g_now_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_dll_process(&ctx);
    assert(iolink_events_pop(&ctx.events, &ev));
    assert(ev.code == IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert(ev.mode == IOLINK_EVENT_MODE_DISAPPEARS);

    return 0;
}
