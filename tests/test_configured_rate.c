/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#undef NDEBUG /* Keep this standalone regression active in Release builds. */
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"

typedef struct
{
    iolink_baudrate_t baud_history[16];
    size_t baud_count;
    iolink_phy_mode_t mode;
    bool wakeup_pending;
    bool rx_error_enabled;
    bool rx_error_sent;
    size_t rx_error_pos;
    uint8_t rx[8];
    size_t rx_len;
    size_t rx_pos;
    uint8_t tx[32];
    size_t tx_len;
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
    test_phy_t* phy = user;
    assert(len <= sizeof(phy->tx) - phy->tx_len);
    (void) memcpy(&phy->tx[phy->tx_len], data, len);
    phy->tx_len += len;
    return (int) len;
}

static int test_phy_recv_byte(void* user, uint8_t* byte)
{
    test_phy_t* phy = user;
    if (phy->rx_error_enabled && !phy->rx_error_sent && (phy->rx_pos == phy->rx_error_pos)) {
        phy->rx_error_sent = true;
        return -1;
    }
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

static void init_operate_type1_device(test_device_t* device)
{
    (void) memset(device, 0, sizeof(*device));
    device->config.phy = test_phy_api;
    device->config.phy.user = &device->phy;
    device->config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_1_1;
    device->config.stack.pd_in_len = 1U;
    device->config.stack.pd_out_len = 1U;
    assert(iolink_device_init(&device->ctx, &device->config) == 0);
    device->ctx.dll.state = IOLINK_DLL_STATE_OPERATE;
    device->ctx.dll.phy_mode = IOLINK_PHY_MODE_SDCI;
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
        device.ctx.dll.state = IOLINK_DLL_STATE_OPERATE;
        device.ctx.dll.phy_mode = IOLINK_PHY_MODE_SDCI;
        device.ctx.dll.last_activity_ms = 1000U;
        iolink_device_process(&device.ctx);

        assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
        assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SDCI);
        assert(device.ctx.dll.timeout_errors == 1U);
        assert_recorded_rate(&device.phy, expected_baudrate(config_rates[i]));

        /* A UART without a wake-up detector stays in its established SDCI
         * transport and can accept the next fresh startup probe. */
        uint8_t probe[2];
        make_type0_frame(probe, 0xA2U, 0U, false);
        feed_frame(&device, probe, sizeof(probe));
        assert(device.ctx.dll.state == IOLINK_DLL_STATE_PREOPERATE);
        assert(device.phy.tx_len > 0U);
    }
}

static void test_expired_idle_discards_queued_request(void)
{
    test_device_t device;
    g_now_us = 5001000U;
    init_operate_type1_device(&device);

    uint8_t request[4] = {0x00U, IOLINK_MSEQ_TYPE_1, 0x5AU, 0x00U};
    request[1] = (uint8_t) (request[1] | iolink_checksum6(request, sizeof(request)));
    (void) memcpy(device.phy.rx, request, sizeof(request));
    device.phy.rx_len = sizeof(request);
    device.ctx.dll.last_activity_ms = 4000U;

    iolink_device_process(&device.ctx);

    assert(device.ctx.dll.timeout_errors == 1U);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
    assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
    assert(device.phy.tx_len == 0U);
    assert(device.phy.rx_pos == device.phy.rx_len);
}

static void test_invalid_frame_does_not_refresh_inactivity(void)
{
    test_device_t device;
    g_now_us = 5000999U;
    init_operate_type1_device(&device);

    uint8_t invalid[4] = {0x00U, IOLINK_MSEQ_TYPE_1, 0x5AU, 0x00U};
    invalid[1] = (uint8_t) (invalid[1] | (iolink_checksum6(invalid, sizeof(invalid)) ^ 0x01U));
    (void) memcpy(device.phy.rx, invalid, sizeof(invalid));
    device.phy.rx_len = sizeof(invalid);
    device.ctx.dll.last_activity_ms = 4000U;

    iolink_device_process(&device.ctx);
    assert(device.ctx.dll.last_activity_ms == 4000U);
    assert(device.phy.tx_len == 0U);

    g_now_us = 5001001U;
    iolink_device_process(&device.ctx);
    assert(device.ctx.dll.timeout_errors == 1U);
    assert(device.ctx.dll.state == IOLINK_DLL_STATE_STARTUP);
    assert(device.ctx.dll.phy_mode == IOLINK_PHY_MODE_SIO);
}

static void test_rx_error_discards_partial_and_queued_frame_bytes(void)
{
    test_device_t device;
    g_now_us = 5000000U;
    init_operate_type1_device(&device);

    uint8_t request[4] = {0x00U, IOLINK_MSEQ_TYPE_1, 0x5AU, 0x00U};
    request[1] = (uint8_t) (request[1] | iolink_checksum6(request, sizeof(request)));
    (void) memcpy(device.phy.rx, request, sizeof(request));
    device.phy.rx_len = sizeof(request);
    device.phy.rx_error_enabled = true;
    device.phy.rx_error_pos = 2U;
    device.ctx.dll.last_activity_ms = 5000U;

    iolink_device_process(&device.ctx);

    assert(device.phy.rx_error_sent);
    assert(device.phy.rx_pos == device.phy.rx_len);
    assert(device.ctx.dll.frame_index == 0U);
    assert(device.ctx.dll.framing_errors == 1U);
    assert(device.phy.tx_len == 0U);

    device.phy.rx_error_enabled = false;
    device.phy.rx_len = sizeof(request);
    device.phy.rx_pos = 0U;
    iolink_device_process(&device.ctx);
    assert(device.phy.tx_len > 0U);
    assert(device.ctx.dll.last_activity_ms == 5000U);
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

static uint8_t vendor_register;

static uint8_t test_vendor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                                   const uint8_t* input, size_t length, uint8_t* output,
                                   size_t* output_length)
{
    (void) user;
    if (index != 0x0100U) return IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL;
    if (subindex != 0U) return IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
    if (write) {
        if ((input == NULL) || (length != 1U)) return IOLINK_ISDU_ERROR_LENGTH_OVERRUN;
        vendor_register = input[0];
        *output_length = 0U;
        return 0U;
    }
    if ((output == NULL) || (output_length == NULL) || (*output_length < 1U)) {
        return IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
    }
    output[0] = vendor_register;
    *output_length = 1U;
    return 0U;
}

static void init_customer_profile_device(test_device_t* device)
{
    static const iolink_device_info_t identity = {
        .vendor_name = "Rig",
        .vendor_text = "COM3 customer profile",
        .product_name = "TYPE_2_V PDin4 OD1",
        .product_id = "COM3-2V-4-1",
        .product_text = "Wire regression",
        .serial_number = "COM3-001",
        .hardware_revision = "1",
        .firmware_revision = "1",
        .vendor_id = 0x0123U,
        .device_id = 0x010203U,
        .min_cycle_time = 10U,
        .revision_id = 0x11U,
    };
    static const uint8_t pdin[] = {0x12U, 0x34U, 0x56U, 0x78U};

    (void) memset(device, 0, sizeof(*device));
    device->config.phy = test_phy_api;
    device->config.phy.user = &device->phy;
    device->config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    device->config.stack.pd_in_len = 4U;
    device->config.stack.pd_out_len = 1U;
    device->config.stack.od_len = 1U;
    device->config.stack.transmission_rate = IOLINK_DEVICE_RATE_COM3;
    device->config.device_info = &identity;
    device->config.vendor_service = test_vendor_service;
    device->config.user = device;
    vendor_register = 0U;
    assert(iolink_device_init(&device->ctx, &device->config) == 0);
    assert(iolink_device_pd_input_update(&device->ctx, pdin, sizeof(pdin), true) == 0);
}

static size_t exchange_frame(test_device_t* device, const uint8_t* frame, size_t frame_len,
                             uint8_t* response, size_t response_capacity)
{
    assert(frame_len <= sizeof(device->phy.rx));
    (void) memcpy(device->phy.rx, frame, frame_len);
    device->phy.rx_len = frame_len;
    device->phy.rx_pos = 0U;
    device->phy.tx_len = 0U;
    iolink_device_process(&device->ctx);
    assert(device->phy.tx_len <= response_capacity);
    (void) memcpy(response, device->phy.tx, device->phy.tx_len);
    return device->phy.tx_len;
}

static size_t make_type0_read(uint8_t mc, uint8_t* frame)
{
    frame[0] = mc;
    frame[1] = 0U;
    frame[1] = iolink_checksum6(frame, 2U);
    return 2U;
}

static size_t make_type0_write(uint8_t mc, uint8_t od, uint8_t* frame)
{
    frame[0] = mc;
    frame[1] = 0U;
    frame[2] = od;
    frame[1] = iolink_checksum6(frame, 3U);
    return 3U;
}

static size_t make_type2_frame(uint8_t mc, uint8_t pdout, bool write_od, uint8_t od, uint8_t* frame)
{
    const size_t len = write_od ? 4U : 3U;
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[2] = pdout;
    if (write_od) frame[3] = od;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
    return len;
}

static void assert_type0_reply(const uint8_t* response, size_t len)
{
    uint8_t msg[2];
    assert(len == sizeof(msg));
    msg[0] = response[0];
    msg[1] = (uint8_t) (response[1] & 0xC0U);
    assert((response[1] & IOLINK_MSEQ_CHECKSUM_MASK) == iolink_checksum6(msg, sizeof(msg)));
}

static void assert_operate_reply(const uint8_t* response, size_t len, uint8_t od_len)
{
    static const uint8_t expected_pdin[] = {0x12U, 0x34U, 0x56U, 0x78U};
    iolink_frame_operate_response_t decoded;
    assert(iolink_frame_decode_operate_response(response, len, 4U, od_len, &decoded) == 0);
    assert(decoded.checksum_ok);
    assert(decoded.pd_valid);
    assert(decoded.pd_len == sizeof(expected_pdin));
    assert(memcmp(decoded.pd, expected_pdin, sizeof(expected_pdin)) == 0);
    if (od_len > 0U) assert(decoded.od_len == od_len);
}

static void exchange_type2_isdu_write_octet(test_device_t* device, uint8_t flowctrl, uint8_t octet)
{
    uint8_t frame[4];
    uint8_t response[8];
    const uint8_t mc = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl);
    const size_t frame_len = make_type2_frame(mc, 0xA5U, true, octet, frame);
    const size_t response_len =
        exchange_frame(device, frame, frame_len, response, sizeof(response));
    assert_operate_reply(response, response_len, 0U);
    uint8_t pdout = 0U;
    assert(iolink_device_pd_output_read(&device->ctx, &pdout, 1U) == 1);
    assert(pdout == 0xA5U);
}

static void send_isdu_request(test_device_t* device, const uint8_t* request, size_t request_len)
{
    for (size_t i = 0U; i < request_len; ++i) {
        const uint8_t flowctrl =
            (i == 0U) ? IOLINK_FLOWCTRL_START : (uint8_t) (i & IOLINK_FLOWCTRL_COUNT_MASK);
        exchange_type2_isdu_write_octet(device, flowctrl, request[i]);
    }
}

static size_t read_isdu_response(test_device_t* device, uint8_t* output, size_t capacity)
{
    uint8_t first = 0U;
    size_t total = 0U;
    size_t count = 0U;
    uint8_t flowctrl = IOLINK_FLOWCTRL_START;

    while ((count < capacity) && ((total == 0U) || (count < total))) {
        uint8_t frame[4];
        uint8_t response[8];
        iolink_frame_operate_response_t decoded;
        const uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | flowctrl);
        const size_t frame_len = make_type2_frame(mc, 0x5AU, false, 0U, frame);
        const size_t response_len =
            exchange_frame(device, frame, frame_len, response, sizeof(response));
        assert(iolink_frame_decode_operate_response(response, response_len, 4U, 1U, &decoded) == 0);
        assert(decoded.checksum_ok);
        assert(decoded.pd_valid);
        assert(decoded.pd_len == 4U);
        assert(memcmp(decoded.pd, (uint8_t[]){0x12U, 0x34U, 0x56U, 0x78U}, 4U) == 0);
        output[count++] = decoded.od[0];
        if (count == 1U) {
            first = decoded.od[0];
            assert(first != 0x01U); /* No Busy response expected after synchronous dispatch. */
            const uint8_t length = (uint8_t) (first & 0x0FU);
            total = (length == 1U) ? 0U : (length == 0U ? 1U : length);
        }
        else if (((first & 0x0FU) == 1U) && (count == 2U)) {
            total = output[1];
        }
        if (flowctrl == IOLINK_FLOWCTRL_START) {
            flowctrl = 1U;
        }
        else {
            flowctrl = (uint8_t) ((flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        }
        if (total == 0U && count == capacity) assert(false);
    }
    assert(total != 0U);
    assert(count == total);
    return count;
}

static void check_isdu_read(test_device_t* device, uint16_t index, const uint8_t* expected,
                            size_t expected_len)
{
    uint8_t request[5] = {0xB5U, (uint8_t) (index >> 8), (uint8_t) index, 0U, 0U};
    uint8_t response[16];
    for (size_t i = 0U; i < 4U; ++i) request[4] ^= request[i];
    send_isdu_request(device, request, sizeof(request));
    const size_t response_len = read_isdu_response(device, response, sizeof(response));
    assert(response[0] == (uint8_t) (0xD0U | (expected_len + 2U)));
    assert(response_len == expected_len + 2U);
    assert(memcmp(&response[1], expected, expected_len) == 0);
    uint8_t chkpdu = 0U;
    for (size_t i = 0U; i < response_len; ++i) chkpdu ^= response[i];
    assert(chkpdu == 0U);
}

static void check_isdu_write_and_ack(test_device_t* device, uint16_t index, uint8_t value)
{
    uint8_t request[6] = {0x36U, (uint8_t) (index >> 8), (uint8_t) index, 0U, value, 0U};
    uint8_t response[4];
    for (size_t i = 0U; i < 5U; ++i) request[5] ^= request[i];
    send_isdu_request(device, request, sizeof(request));
    const size_t response_len = read_isdu_response(device, response, sizeof(response));
    assert(response_len == 2U);
    assert(response[0] == 0x52U);
    assert((uint8_t) (response[0] ^ response[1]) == 0U);
}

static void enter_operate_customer_profile(test_device_t* device)
{
    uint8_t frame[8];
    uint8_t response[8];
    size_t len;

    device->phy.wakeup_pending = true;
    iolink_device_process(&device->ctx);
    assert(iolink_device_get_phy_mode(&device->ctx) == IOLINK_PHY_MODE_SDCI);
    assert(iolink_device_get_baudrate(&device->ctx) == IOLINK_BAUDRATE_COM3);

    len = make_type0_read(0xA2U, frame);
    assert_type0_reply(response, exchange_frame(device, frame, len, response, sizeof(response)));
    assert(iolink_device_get_state(&device->ctx) == IOLINK_DLL_STATE_PREOPERATE);

    len = make_type0_write(IOLINK_MC_CHANNEL_PAGE, IOLINK_CMD_DEVICE_OPERATE, frame);
    const size_t ack_len = exchange_frame(device, frame, len, response, sizeof(response));
    assert(ack_len == 1U);
    const uint8_t cks_input[] = {(uint8_t) (response[0] & 0xC0U)};
    assert((response[0] & IOLINK_MSEQ_CHECKSUM_MASK) ==
           iolink_checksum6(cks_input, sizeof(cks_input)));
    assert(iolink_device_get_state(&device->ctx) == IOLINK_DLL_STATE_ESTAB_COM);

    len = make_type2_frame(IOLINK_MC_CHANNEL_PROCESS, 0x3CU, true, 0U, frame);
    const size_t operate_len = exchange_frame(device, frame, len, response, sizeof(response));
    assert_operate_reply(response, operate_len, 0U);
    assert(iolink_device_get_state(&device->ctx) == IOLINK_DLL_STATE_OPERATE);
    uint8_t pdout = 0U;
    assert(iolink_device_pd_output_read(&device->ctx, &pdout, 1U) == 1);
    assert(pdout == 0x3CU);
}

static void test_legacy_setter_survives_wakeup_and_recovery(void)
{
    test_device_t device;
    uint8_t bad_frame[2];
    g_now_us = 5000000U;
    init_device(&device, IOLINK_DEVICE_RATE_DEFAULT, true);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM2);
    assert(iolink_dll_set_baudrate(&device.ctx.dll, IOLINK_BAUDRATE_COM3) == 0);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);

    device.phy.wakeup_pending = true;
    iolink_device_process(&device.ctx);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);
    make_type0_frame(bad_frame, 0U, 0U, false);
    bad_frame[1] ^= 1U;
    for (uint8_t retry = 0U; retry < 3U; ++retry) feed_frame(&device, bad_frame, sizeof(bad_frame));
    assert(iolink_device_get_state(&device.ctx) == IOLINK_DLL_STATE_STARTUP);
    assert(iolink_device_get_phy_mode(&device.ctx) == IOLINK_PHY_MODE_SIO);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);

    device.phy.wakeup_pending = true;
    iolink_device_process(&device.ctx);
    assert(iolink_device_get_phy_mode(&device.ctx) == IOLINK_PHY_MODE_SDCI);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);
}

static void test_customer_com3_type2v_pd4_od1_wire_and_recovery(void)
{
    test_device_t device;
    uint8_t frame[8];
    uint8_t response[8];
    const uint8_t vendor_name[] = {'R', 'i', 'g'};
    const uint8_t written_register[] = {0xA6U};

    g_now_us = 5000000U;
    init_customer_profile_device(&device);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);
    assert(device.ctx.dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V);
    assert(device.ctx.dll.pd_in_len == 4U);
    assert(device.ctx.dll.od_len == 1U);
    enter_operate_customer_profile(&device);

    check_isdu_read(&device, IOLINK_IDX_VENDOR_NAME, vendor_name, sizeof(vendor_name));
    check_isdu_write_and_ack(&device, 0x0100U, written_register[0]);
    check_isdu_read(&device, 0x0100U, written_register, sizeof(written_register));

    for (uint8_t retry = 0U; retry < 3U; ++retry) {
        const size_t len = make_type2_frame(IOLINK_MC_CHANNEL_PROCESS, 0x77U, true, 0U, frame);
        frame[1] ^= 1U; /* A bad CKT forces the real fallback/recovery path. */
        assert(exchange_frame(&device, frame, len, response, sizeof(response)) == 0U);
    }
    assert(iolink_device_get_state(&device.ctx) == IOLINK_DLL_STATE_STARTUP);
    assert(iolink_device_get_phy_mode(&device.ctx) == IOLINK_PHY_MODE_SIO);
    assert(iolink_device_get_baudrate(&device.ctx) == IOLINK_BAUDRATE_COM3);

    enter_operate_customer_profile(&device);
    check_isdu_read(&device, IOLINK_IDX_VENDOR_NAME, vendor_name, sizeof(vendor_name));
    check_isdu_write_and_ack(&device, 0x0100U, 0x5CU);
    check_isdu_read(&device, 0x0100U, (uint8_t[]){0x5CU}, 1U);
}

int main(void)
{
    test_zero_default_and_configured_startup_wakeup();
    test_error_fallback_restores_configured_rate();
    test_inactivity_restores_configured_rate();
    test_expired_idle_discards_queued_request();
    test_invalid_frame_does_not_refresh_inactivity();
    test_rx_error_discards_partial_and_queued_frame_bytes();
    test_dsio_timeout_restores_configured_rate();
    test_master_fallback_restores_configured_rate();
    test_invalid_configured_rate_is_rejected();
    test_legacy_setter_survives_wakeup_and_recovery();
    test_customer_com3_type2v_pd4_od1_wire_and_recovery();
    return 0;
}
