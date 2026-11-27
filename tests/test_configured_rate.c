/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"

typedef struct
{
    iolink_baudrate_t baud_history[16];
    size_t baud_count;
    iolink_phy_mode_t mode;
    bool wakeup_pending;
    uint8_t rx[8];
    size_t rx_len;
    size_t rx_pos;
} test_phy_t;

typedef struct
{
    test_phy_t phy;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
} test_device_t;

static uint64_t g_now_us;

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (g_now_us / 1000U);
}

uint64_t iolink_time_get_us(void)
{
    return g_now_us;
}

static int test_phy_init(void* user)
{
    (void) user;
    return 0;
}

static void test_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    test_phy_t* phy = user;
    phy->mode = mode;
}

static void test_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    test_phy_t* phy = user;
    assert(phy->baud_count < sizeof(phy->baud_history) / sizeof(phy->baud_history[0]));
    phy->baud_history[phy->baud_count++] = baudrate;
}

static int test_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    return (int) len;
}

static int test_phy_recv_byte(void* user, uint8_t* byte)
{
    test_phy_t* phy = user;
    if (phy->rx_pos >= phy->rx_len) {
        return 0;
    }
    *byte = phy->rx[phy->rx_pos++];
    return 1;
}

static int test_phy_detect_wakeup(void* user)
{
    test_phy_t* phy = user;
    const int detected = phy->wakeup_pending ? 1 : 0;
    phy->wakeup_pending = false;
    return detected;
}

static const iolink_phy_api_t test_phy_api = {
    .init = test_phy_init,
    .set_mode = test_phy_set_mode,
    .set_baudrate = test_phy_set_baudrate,
    .send = test_phy_send,
    .recv_byte = test_phy_recv_byte,
    .detect_wakeup = test_phy_detect_wakeup,
};

static iolink_baudrate_t expected_baudrate(iolink_device_rate_t rate)
{
    switch (rate) {
        case IOLINK_DEVICE_RATE_COM1:
            return IOLINK_BAUDRATE_COM1;
        case IOLINK_DEVICE_RATE_COM3:
            return IOLINK_BAUDRATE_COM3;
        case IOLINK_DEVICE_RATE_DEFAULT:
        case IOLINK_DEVICE_RATE_COM2:
        default:
            return IOLINK_BAUDRATE_COM2;
    }
}

static void init_device(test_device_t* device, iolink_device_rate_t rate, bool detect_wakeup)
{
    (void) memset(device, 0, sizeof(*device));
    device->config.phy = test_phy_api;
    device->config.phy.user = &device->phy;
    if (!detect_wakeup) {
        device->config.phy.detect_wakeup = NULL;
    }
    if (rate != IOLINK_DEVICE_RATE_DEFAULT) {
        device->config.stack.transmission_rate = rate;
    }
    assert(iolink_device_init(&device->ctx, &device->config) == 0);
}

static void assert_recorded_rate(const test_phy_t* phy, iolink_baudrate_t rate)
{
    assert(phy->baud_count > 0U);
    for (size_t i = 0U; i < phy->baud_count; ++i) {
        assert(phy->baud_history[i] == rate);
    }
}

static void feed_frame(test_device_t* device, const uint8_t* frame, size_t len)
{
    assert(len <= sizeof(device->phy.rx));
    (void) memcpy(device->phy.rx, frame, len);
    device->phy.rx_len = len;
    device->phy.rx_pos = 0U;
    iolink_device_process(&device->ctx);
}

static void make_type0_frame(uint8_t* frame, uint8_t mc, uint8_t od, bool include_od)
{
    const size_t len = include_od ? 3U : 2U;
    frame[0] = mc;
    frame[1] = 0U;
    if (include_od) {
        frame[2] = od;
    }
    frame[1] = iolink_checksum6(frame, len);
}

static void test_zero_default_and_configured_startup_wakeup(void)
{
    const iolink_device_rate_t config_rates[] = {
        IOLINK_DEVICE_RATE_DEFAULT,
        IOLINK_DEVICE_RATE_COM1,
        IOLINK_DEVICE_RATE_COM2,
        IOLINK_DEVICE_RATE_COM3,
    };

    for (size_t i = 0U; i < sizeof(config_rates) / sizeof(config_rates[0]); ++i) {
        test_device_t device;
        g_now_us = 5000000U;
        init_device(&device, config_rates[i], true);
        const iolink_baudrate_t expected = expected_baudrate(config_rates[i]);
        assert(device.phy.baud_count == 1U);
        assert_recorded_rate(&device.phy, expected);
        assert(iolink_device_get_baudrate(&device.ctx) == expected);

        device.phy.wakeup_pending = true;
        iolink_device_process(&device.ctx);
        assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SDCI);
        assert(device.phy.baud_count == 2U);
        assert_recorded_rate(&device.phy, expected);
    }
}

static void test_error_fallback_restores_configured_rate(void)
{
    const iolink_device_rate_t config_rates[] = {
        IOLINK_DEVICE_RATE_COM1,
        IOLINK_DEVICE_RATE_COM2,
        IOLINK_DEVICE_RATE_COM3,
    };

    for (size_t i = 0U; i < sizeof(config_rates) / sizeof(config_rates[0]); ++i) {
        test_device_t device;
        g_now_us = 5000000U;
        init_device(&device, config_rates[i], false);
        uint8_t bad_frame[2];
        make_type0_frame(bad_frame, 0U, 0U, false);
        bad_frame[1] ^= 1U;

        for (uint8_t retry = 0U; retry < 3U; ++retry) {
            feed_frame(&device, bad_frame, sizeof(bad_frame));
        }

        assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
        assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
        assert(device.ctx.dll.fallback_count == 0U);
        assert_recorded_rate(&device.phy, expected_baudrate(config_rates[i]));
    }
}

static void test_inactivity_restores_configured_rate(void)
{
    const iolink_device_rate_t config_rates[] = {
        IOLINK_DEVICE_RATE_COM1,
        IOLINK_DEVICE_RATE_COM2,
        IOLINK_DEVICE_RATE_COM3,
    };

    for (size_t i = 0U; i < sizeof(config_rates) / sizeof(config_rates[0]); ++i) {
        test_device_t device;
        g_now_us = 5000000U;
        init_device(&device, config_rates[i], false);
        device.ctx.dll.last_activity_ms = 1000U;
        iolink_device_process(&device.ctx);

        assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
        assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
        assert(device.ctx.dll.timeout_errors == 1U);
        assert_recorded_rate(&device.phy, expected_baudrate(config_rates[i]));
    }
}

static void test_dsio_timeout_restores_configured_rate(void)
{
    test_device_t device;
    g_now_us = 5000000U;
    init_device(&device, IOLINK_DEVICE_RATE_COM3, true);
    device.phy.wakeup_pending = true;
    iolink_device_process(&device.ctx);
    const uint64_t deadline = device.ctx.dll.dsio_deadline_ms;
    assert(deadline != 0U);

    g_now_us = (deadline + 1U) * 1000U;
    iolink_device_process(&device.ctx);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
    assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
    assert_recorded_rate(&device.phy, IOLINK_BAUDRATE_COM3);
}

static void test_master_fallback_restores_configured_rate(void)
{
    test_device_t device;
    uint8_t frame[3];
    g_now_us = 5000000U;
    init_device(&device, IOLINK_DEVICE_RATE_COM3, false);

    make_type0_frame(frame, IOLINK_MC_TRANSITION_COMMAND, 0U, false);
    feed_frame(&device, frame, 2U);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_ESTAB_COM);

    make_type0_frame(frame, 0U, 0U, false);
    feed_frame(&device, frame, 2U);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_OPERATE);

    make_type0_frame(frame, IOLINK_MC_CHANNEL_PAGE, IOLINK_CMD_FALLBACK, true);
    feed_frame(&device, frame, 3U);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_FALLBACK);
    const uint64_t deadline = device.ctx.dll.fallback_deadline_ms;
    assert(deadline > 5000U);

    g_now_us = (deadline + 1U) * 1000U;
    iolink_device_process(&device.ctx);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
    assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
    assert_recorded_rate(&device.phy, IOLINK_BAUDRATE_COM3);
}

static void test_invalid_configured_rate_is_rejected(void)
{
    test_device_t device;
    (void) memset(&device, 0, sizeof(device));
    device.config.phy = test_phy_api;
    device.config.phy.user = &device.phy;
    device.config.stack.transmission_rate = (iolink_device_rate_t) 4U;
    assert(iolink_device_init(&device.ctx, &device.config) != 0);
    assert(device.phy.baud_count == 0U);
}

int main(void)
{
    test_zero_default_and_configured_startup_wakeup();
    test_error_fallback_restores_configured_rate();
    test_inactivity_restores_configured_rate();
    test_dsio_timeout_restores_configured_rate();
    test_master_fallback_restores_configured_rate();
    test_invalid_configured_rate_is_rejected();
    return 0;
}
