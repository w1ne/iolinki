/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic monotonic clock: protocol boundary tests, not physical captures.
 */
#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now;
static uint8_t request[4];
static size_t request_length, rx_index, send_count, last_tx_length;
static uint32_t response_delay, send_duration, mode_delay;
static int sample_result, sample_fault, send_result, pending_wake;
static iolink_phy_timing_quality_t sample_quality;
static iolink_phy_response_timing_t sample;

uint64_t iolink_time_get_us(void)
{
    return now;
}
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now / 1000U);
}

static int receive(void* user, uint8_t* byte)
{
    (void) user;
    if (rx_index == request_length) return 0;
    *byte = request[rx_index++];
    now += 287U; /* COM2 11-bit frame rounded up; no artificial long byte gap. */
    if (rx_index == request_length) sample.rx_end_us = now;
    return 1;
}

static int send_data(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    assert(data != NULL);
    ++send_count;
    last_tx_length = len;
    sample.tx_start_us = sample.rx_end_us + response_delay;
    sample.quality = sample_quality;
    now = sample.tx_start_us + send_duration;
    if (sample_fault == 1) sample.tx_start_us = sample.rx_end_us - 1U;
    if (sample_fault == 2) sample.tx_start_us = now + 1U;
    if (sample_fault == 3) {
        sample.rx_end_us = 0U;
        sample.tx_start_us = (uint64_t) UINT32_MAX + 1U;
        now = sample.tx_start_us + send_duration;
    }
    return send_result ? send_result : (int) len;
}

static int timing(void* user, iolink_phy_response_timing_t* out)
{
    (void) user;
    *out = sample;
    return sample_result;
}
static void set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    if (mode == IOLINK_PHY_MODE_SDCI) now += mode_delay;
}
static int wake(void* user)
{
    (void) user;
    const int event = pending_wake;
    pending_wake = 0;
    return event;
}
static iolink_phy_api_t phy = {
    .send = send_data, .recv_byte = receive, .set_mode = set_mode, .get_response_timing = timing};

static void setup(iolink_dll_ctx_t* ctx, iolink_baudrate_t baud, bool type0)
{
    now = 100000U;
    rx_index = send_count = last_tx_length = mode_delay = 0U;
    response_delay = 52U;
    send_duration = 860U; /* A valid three-byte COM2 reply takes >500 us. */
    sample_result = 1;
    sample_fault = send_result = pending_wake = 0;
    sample_quality = IOLINK_PHY_TIMING_CAPTURED;
    memset(&sample, 0, sizeof(sample));
    phy.detect_wakeup = NULL;
    phy.get_response_timing = timing;
    iolink_dll_init(ctx, &phy);
    ctx->state = IOLINK_DLL_STATE_OPERATE;
    ctx->m_seq_type = type0 ? IOLINK_M_SEQ_TYPE_0 : IOLINK_M_SEQ_TYPE_1_1;
    ctx->pd_in_len_current = (uint8_t) (type0 ? 0U : 1U);
    ctx->pd_out_len_current = 0U;
    ctx->enforce_timing = true;
    ctx->t_byte_limit_us = 100000U;
    ctx->baudrate = baud;
    request[0] = 0x80U;
    request[1] = type0 ? IOLINK_MSEQ_TYPE_0 : IOLINK_MSEQ_TYPE_1;
    request[2] = 0U;
    request_length = type0 ? 2U : 3U;
    request[1] |= iolink_checksum6(request, request_length);
}

static void boundaries(void)
{
    const struct
    {
        iolink_baudrate_t baud;
        uint32_t min, max;
    } cases[] = {{IOLINK_BAUDRATE_COM1, 209U, 2083U},
                 {IOLINK_BAUDRATE_COM2, 27U, 260U},
                 {IOLINK_BAUDRATE_COM3, 5U, 43U}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const uint32_t delays[] = {cases[i].min - 1U, cases[i].min, cases[i].max,
                                   cases[i].max + 1U};
        for (unsigned j = 0; j < 4; ++j) {
            iolink_dll_ctx_t ctx;
            setup(&ctx, cases[i].baud, false);
            response_delay = delays[j];
            iolink_dll_process(&ctx);
            assert(send_count == 1U && last_tx_length == 3U);
            assert(ctx.t_a_violations == ((j == 0U || j == 3U) ? 1U : 0U));
            assert(ctx.t_ren_violations == 0U);
            assert(ctx.response_time_us == delays[j]);
        }
    }
}
static void blocking_and_type0(void)
{
    for (unsigned type0 = 0; type0 < 2; ++type0) {
        iolink_dll_ctx_t ctx;
        setup(&ctx, IOLINK_BAUDRATE_COM2, type0 != 0U);
        send_duration = 20000U;
        iolink_dll_process(&ctx);
        assert(send_count == 1U);
        assert(last_tx_length == (type0 ? 2U : 3U));
        assert(ctx.timing_errors == 0U);
        assert(ctx.response_time_us == 52U);
        assert(ctx.response_timing_captured == 1U);
        assert(ctx.response_timing_quality == IOLINK_PHY_TIMING_CAPTURED);
    }
}
static void missing_invalid_and_estimated(void)
{
    for (unsigned i = 0; i < 7; ++i) {
        iolink_dll_ctx_t ctx;
        setup(&ctx, IOLINK_BAUDRATE_COM2, false);
        if (i == 0U) phy.get_response_timing = NULL;
        if (i == 1U) sample_result = 0;
        if (i == 2U) sample_result = -1;
        if (i == 3U) sample_quality = IOLINK_PHY_TIMING_UNAVAILABLE;
        if (i >= 4U) sample_fault = (int) i - 3;
        iolink_dll_process(&ctx);
        assert(ctx.response_timing_unavailable == 1U);
        assert(ctx.response_timing_captured == 0U);
        assert(ctx.response_timing_quality == IOLINK_PHY_TIMING_UNAVAILABLE);
        const bool unavailable_only = (i == 0U) || (i == 1U);
        assert(ctx.timing_errors == (unavailable_only ? 0U : 1U));
        if (unavailable_only) {
            assert(!iolink_events_pending(&ctx.events));
        }
        else {
            iolink_event_t event;
            assert(iolink_events_pop(&ctx.events, &event));
            assert(event.code == IOLINK_EVENT_COMM_TIMING);
            assert(!iolink_events_pending(&ctx.events));
        }
    }
    iolink_dll_ctx_t ctx;
    setup(&ctx, IOLINK_BAUDRATE_COM2, false);
    sample_quality = IOLINK_PHY_TIMING_ESTIMATED;
    iolink_dll_process(&ctx);
    assert(ctx.response_timing_estimated == 1U);
    assert(ctx.response_timing_captured == 0U);
    assert(ctx.timing_errors == 0U);
    setup(&ctx, IOLINK_BAUDRATE_COM2, false);
    send_result = -1;
    iolink_dll_process(&ctx);
    assert(ctx.framing_errors == 1U && ctx.response_timing_captured == 0U);
}

static void missing_hook_many_frames_preserves_real_violation(void)
{
    iolink_dll_ctx_t ctx;
    setup(&ctx, IOLINK_BAUDRATE_COM2, false);
    phy.get_response_timing = NULL;

    for (unsigned i = 0U; i < 32U; ++i) {
        rx_index = 0U;
        iolink_dll_process(&ctx);
    }

    assert(send_count == 32U);
    assert(ctx.response_timing_unavailable == 32U);
    assert(ctx.timing_errors == 0U);
    assert(ctx.t_a_violations == 0U);
    assert(!iolink_events_pending(&ctx.events));
    assert(iolink_events_dropped(&ctx.events) == 0U);

    /* A fresh, valid timestamp outside 1..10 bit times is a real violation. */
    phy.get_response_timing = timing;
    response_delay = 0U;
    rx_index = 0U;
    iolink_dll_process(&ctx);

    assert(send_count == 33U);
    assert(ctx.response_timing_unavailable == 32U);
    assert(ctx.timing_errors == 1U);
    assert(ctx.t_a_violations == 1U);
    assert(iolink_events_dropped(&ctx.events) == 0U);
    iolink_event_t event;
    assert(iolink_events_pop(&ctx.events, &event));
    assert(event.code == IOLINK_EVENT_COMM_TIMING);
    assert(!iolink_events_pending(&ctx.events));
}

static void wake_receive_enable(void)
{
    for (unsigned slow = 0; slow < 2; ++slow) {
        iolink_dll_ctx_t ctx;
        setup(&ctx, IOLINK_BAUDRATE_COM2, false);
        phy.detect_wakeup = wake;
        ctx.phy_mode = IOLINK_PHY_MODE_SIO;
        ctx.state = IOLINK_DLL_STATE_STARTUP;
        request_length = 0U;
        pending_wake = 1;
        mode_delay = 500U + slow;
        iolink_dll_process(&ctx);
        assert(ctx.t_ren_violations == slow);
        assert(ctx.t_a_violations == 0U);
        iolink_dll_set_t_ren_limit_us(&ctx, 1234U);
        assert(ctx.t_ren_limit_us == 1234U);
        iolink_dll_set_t_ren_limit_us(&ctx, 0U);
        assert(ctx.t_ren_limit_us == 500U && !ctx.t_ren_override);
    }
}
int main(void)
{
    boundaries();
    blocking_and_type0();
    missing_invalid_and_estimated();
    missing_hook_many_frames_preserves_real_violation();
    wake_receive_enable();
    puts("response timing: deterministic boundary/failure scenarios passed");
    return 0;
}
