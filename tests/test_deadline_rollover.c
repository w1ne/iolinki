/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include "iolinki/dll.h"
#include "iolinki/time_utils.h"
static uint64_t now;
static int wake_pending;
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now / 1000U);
}
uint64_t iolink_time_get_us(void)
{
    return now;
}
static int wake(void* user)
{
    (void) user;
    int result = wake_pending;
    wake_pending = 0;
    return result;
}
static int receive(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}
int main(void)
{
    const iolink_phy_api_t phy = {.detect_wakeup = wake, .recv_byte = receive};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx, &phy);
    now = ((uint64_t) UINT32_MAX - 100U) * 1000U;
    wake_pending = 1;
    iolink_dll_process(&ctx);
    assert(ctx.state == IOLINK_DLL_STATE_AWAITING_COMM);
    uint64_t deadline = now / 1000U + IOLINK_T_DSIO_MS;
    assert(ctx.dsio_deadline_ms == deadline);
    now += 150000U; /* crossed wrapping millisecond API, deadline still ahead */
    iolink_dll_process(&ctx);
    assert(ctx.state == IOLINK_DLL_STATE_AWAITING_COMM);
    now = (deadline + 1U) * 1000U;
    iolink_dll_process(&ctx);
    assert(ctx.phy_mode == IOLINK_PHY_MODE_SIO);

    ctx.state = IOLINK_DLL_STATE_FALLBACK;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.last_activity_ms = 0;
    ctx.fallback_deadline_ms = now / 1000U + 50U;
    iolink_dll_process(&ctx);
    assert(ctx.phy_mode == IOLINK_PHY_MODE_SDCI);
    now += 50000U;
    iolink_dll_process(&ctx);
    assert(ctx.phy_mode == IOLINK_PHY_MODE_SIO);
    return 0;
}
