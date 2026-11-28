/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deterministic protocol recovery. Synthetic timestamps are not wire evidence.
 */
#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now;
static iolink_baudrate_t configured_rate;
static iolink_phy_mode_t line_mode;
static uint32_t baud;
static int pending_wake;
static uint8_t request[4], reply[8];
static size_t request_length, request_index, reply_length, sends;
static iolink_phy_response_timing_t sample;
uint64_t iolink_time_get_us(void)
{
    return now;
}
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now / 1000U);
}
static void speed(void* user, iolink_baudrate_t rate)
{
    (void) user;
    configured_rate = rate;
    const uint32_t rates[] = {4800U, 38400U, 230400U};
    assert((unsigned) rate < 3U);
    baud = rates[(unsigned) rate];
}
static void mode(void* user, iolink_phy_mode_t value)
{
    (void) user;
    line_mode = value;
}
static int wake(void* user)
{
    (void) user;
    int result = pending_wake;
    pending_wake = 0;
    return result;
}
static int receive(void* user, uint8_t* byte)
{
    (void) user;
    if (request_index == request_length) return 0;
    *byte = request[request_index++];
    now += (11000000U + baud - 1U) / baud;
    sample.rx_end_us = now;
    return 1;
}
static int send_data(void* user, const uint8_t* data, size_t length)
{
    (void) user;
    assert(length <= sizeof(reply));
    memcpy(reply, data, length);
    reply_length = length;
    ++sends;
    sample.tx_start_us = now + (2000000U + baud - 1U) / baud;
    sample.quality = IOLINK_PHY_TIMING_CAPTURED;
    now = sample.tx_start_us + length * ((11000000U + baud - 1U) / baud);
    return (int) length;
}
static int timing(void* user, iolink_phy_response_timing_t* out)
{
    (void) user;
    *out = sample;
    return 1;
}
static const iolink_phy_api_t phy = {.set_baudrate = speed,
                                     .set_mode = mode,
                                     .detect_wakeup = wake,
                                     .recv_byte = receive,
                                     .send = send_data,
                                     .get_response_timing = timing};
static void setup(iolink_dll_ctx_t* ctx, iolink_baudrate_t rate)
{
    now = 100000U;
    pending_wake = 0;
    request_length = request_index = reply_length = sends = 0U;
    memset(&sample, 0, sizeof(sample));
    iolink_dll_init(ctx, &phy);
    assert(iolink_dll_set_baudrate(ctx, rate) == 0);
    ctx->m_seq_type = IOLINK_M_SEQ_TYPE_1_1;
    ctx->pd_in_len_current = 1U;
    ctx->pd_out_len_current = 0U;
    ctx->pd_in[0] = 0xa5U;
    ctx->min_cycle_time_us = 10000U;
    iolink_dll_set_timing_enforcement(ctx, true);
}
static void frame(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t type, uint8_t od, size_t length,
                  bool corrupt)
{
    now += 10000U;
    request[0] = mc;
    request[1] = type;
    request[2] = od;
    request[1] |= iolink_checksum6(request, length);
    if (corrupt) request[1] ^= 1U;
    request_length = length;
    request_index = 0U;
    iolink_dll_process(ctx);
    assert(request_index == request_length);
}
static void assert_rate(const iolink_dll_ctx_t* ctx, iolink_baudrate_t rate)
{
    assert(iolink_dll_get_baudrate(ctx) == rate);
    assert(configured_rate == rate);
}
static void wake_device(iolink_dll_ctx_t* ctx)
{
    assert(line_mode == IOLINK_PHY_MODE_SIO);
    pending_wake = 1;
    iolink_dll_process(ctx);
    assert(line_mode == IOLINK_PHY_MODE_SDCI);
    assert(ctx->state == IOLINK_DLL_STATE_AWAITING_COMM);
    now += 2000U;
}
static void establish(iolink_dll_ctx_t* ctx, iolink_baudrate_t rate)
{
    assert_rate(ctx, rate);
    frame(ctx, 0xa2U, IOLINK_MSEQ_TYPE_0, 0U, 2U, false);
    assert(ctx->state == IOLINK_DLL_STATE_PREOPERATE && reply_length == 2U);
    frame(ctx, 0x20U, IOLINK_MSEQ_TYPE_0, IOLINK_CMD_DEVICE_OPERATE, 3U, false);
    assert(ctx->state == IOLINK_DLL_STATE_ESTAB_COM && reply_length == 1U);
    frame(ctx, 0x80U, IOLINK_MSEQ_TYPE_1, 0U, 3U, false);
    assert(ctx->state == IOLINK_DLL_STATE_OPERATE);
    assert(reply_length == 3U && reply[1] == 0xa5U);
    uint8_t cks = (uint8_t) (reply[reply_length - 1U] & IOLINK_MSEQ_CHECKSUM_MASK);
    reply[reply_length - 1U] &= (uint8_t) ~IOLINK_MSEQ_CHECKSUM_MASK;
    assert(iolink_checksum6(reply, reply_length) == cks);
    assert(ctx->t_a_violations == 0U && ctx->t_ren_violations == 0U);
    assert_rate(ctx, rate);
}
int main(void)
{
    for (unsigned r = 0; r < 3U; ++r) {
        iolink_baudrate_t rate = (iolink_baudrate_t) r;
        for (unsigned failure = 0; failure < 5U; ++failure) {
            iolink_dll_ctx_t ctx;
            setup(&ctx, rate);
            wake_device(&ctx);
            if (failure == 0U) {
                /* No valid first message: T_DSIO returns to driven SIO. */
                now = (ctx.dsio_deadline_ms + 1U) * 1000U;
                iolink_dll_process(&ctx);
            }
            else {
                establish(&ctx, rate);
                if (failure == 1U) {
                    now += 1001000U; /* Established link disappears. */
                    iolink_dll_process(&ctx);
                }
                else if (failure == 2U) {
                    for (unsigned i = 0; i < 3U; ++i)
                        frame(&ctx, 0x80U, IOLINK_MSEQ_TYPE_1, 0U, 3U, true);
                    assert(ctx.crc_errors == 3U);
                }
                else if (failure == 3U) {
                    frame(&ctx, 0x20U, IOLINK_MSEQ_TYPE_1, IOLINK_CMD_FALLBACK, 3U, false);
                    assert(ctx.state == IOLINK_DLL_STATE_FALLBACK);
                    now = ctx.fallback_deadline_ms * 1000U;
                    iolink_dll_process(&ctx);
                }
                else {
                    /* Three interrupted frames reach the same fallback guard. */
                    for (unsigned i = 0; i < 3U; ++i) {
                        request[0] = 0x80U;
                        request_index = 0U;
                        request_length = 1U;
                        iolink_dll_process(&ctx);
                        assert(ctx.frame_index == 1U);
                        now += ctx.t_byte_limit_us + 1U;
                        iolink_dll_process(&ctx);
                    }
                    assert(ctx.t_byte_violations == 3U);
                }
            }
            assert(ctx.state == IOLINK_DLL_STATE_STARTUP);
            assert(line_mode == IOLINK_PHY_MODE_SIO);
            assert_rate(&ctx, rate);
            size_t before = sends;
            wake_device(&ctx);
            establish(&ctx, rate);
            assert(sends == before + 3U);
        }
        /* An early startup CRC failure must also retain the device rate. */
        iolink_dll_ctx_t ctx;
        setup(&ctx, rate);
        wake_device(&ctx);
        frame(&ctx, 0xa2U, IOLINK_MSEQ_TYPE_0, 0U, 2U, false);
        frame(&ctx, 0xa2U, IOLINK_MSEQ_TYPE_0, 0U, 2U, true);
        assert(ctx.state == IOLINK_DLL_STATE_STARTUP);
        assert_rate(&ctx, rate);
        establish(&ctx, rate);
    }
    puts("baud recovery: 18 COM1/COM2/COM3 loss/re-wake scenarios passed");
    return 0;
}
