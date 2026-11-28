/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "protocol_rig.h"
#include "iolinki/device.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Protocol oracle: real iolinki parsing/dispatch, queued byte transport.
 * The clock and UART are deliberate test seams. No host duration or injected
 * timestamp is reported as silicon or LabWired pin timing. */
static unsigned assertion_line;
#define CHECK(expr) do { if (!(expr)) { assertion_line = __LINE__; return false; } } while (0)

typedef struct {
    uint8_t rx[64], tx[64];
    size_t rx_len, rx_pos, tx_len;
    unsigned tx_calls, select_calls;
    uint8_t blocks[34][32];
    iolink_device_config_t config;
} rig_link_t;

static int rig_recv(void* user, uint8_t* byte)
{
    rig_link_t* link = user;
    if (link->rx_pos >= link->rx_len) return 0;
    *byte = link->rx[link->rx_pos++];
    return 1;
}

static int rig_send(void* user, const uint8_t* data, size_t len)
{
    rig_link_t* link = user;
    if (len > sizeof(link->tx)) return -1;
    memcpy(link->tx, data, len);
    link->tx_len = len;
    link->tx_calls++;
    return (int) len;
}

static const uint8_t* rig_select(void* user, const uint8_t* out,
                                  uint8_t out_len, uint8_t in_len, bool* valid)
{
    rig_link_t* link = user;
    link->select_calls++;
    if (out_len != 1U || in_len != 32U || out[0] >= 34U) {
        *valid = false;
        return NULL;
    }
    *valid = true;
    return link->blocks[out[0]];
}

static void rig_exchange(iolink_device_ctx_t* dev, rig_link_t* link,
                          const uint8_t* request, size_t len)
{
    memcpy(link->rx, request, len);
    link->rx_len = len;
    link->rx_pos = 0U;
    link->tx_len = 0U;
    iolink_device_process(dev);
}

static bool rig_init(iolink_device_ctx_t* dev, rig_link_t* link, uint8_t od_width)
{
    memset(link, 0, sizeof(*link));
    for (size_t block = 0; block < 34U; ++block) {
        for (size_t byte = 0; byte < 32U; ++byte) {
            link->blocks[block][byte] = (uint8_t) ((block * 37U + byte * 11U) ^ 0xA5U);
        }
    }
    link->config.phy.user = link;
    link->config.phy.send = rig_send;
    link->config.phy.recv_byte = rig_recv;
    link->config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    link->config.stack.pd_in_len = 32U;
    link->config.stack.pd_out_len = 1U;
    link->config.stack.od_len = od_width;
    link->config.pd_input_select = rig_select;
    link->config.user = link;
    CHECK(iolink_device_init(dev, &link->config) == 0);
    iolink_device_set_timing_enforcement(dev, false);
    /* Legal Type-0 read followed by DeviceOperate, without a wake-up PHY.
       This PHY models a UART behind an already-established SDCI link. */
    uint8_t startup[2] = {0xA2U, 0U};
    startup[1] = iolink_checksum6(startup, sizeof(startup));
    rig_exchange(dev, link, startup, sizeof(startup));
    CHECK(iolink_device_get_state(dev) == IOLINK_DLL_STATE_PREOPERATE);
    uint8_t operate[3] = {0x20U, 0U, IOLINK_CMD_DEVICE_OPERATE};
    operate[1] = iolink_checksum6(operate, sizeof(operate));
    rig_exchange(dev, link, operate, sizeof(operate));
    CHECK(iolink_device_get_state(dev) == IOLINK_DLL_STATE_ESTAB_COM);
    CHECK(link->select_calls == 0U);
    return true;
}

static void rig_pd_request(uint8_t* req, uint8_t selector, bool write)
{
    req[0] = write ? 0U : 0x80U;
    req[1] = IOLINK_MSEQ_TYPE_2;
    req[2] = selector;
    if (write) req[3] = 0U;
    req[1] |= iolink_checksum6(req, write ? 4U : 3U);
}

static bool exact_profile(void)
{
    iolink_device_ctx_t dev;
    rig_link_t link;
    CHECK(rig_init(&dev, &link, 1U));
    CHECK(dev.dll.od_len == 1U);
    CHECK(iolink_isdu_direct_param_page1_octet(&dev.dll.isdu, 3U) == 0x09U);
    CHECK(iolink_isdu_direct_param_page1_octet(&dev.dll.isdu, 5U) == 0x9FU);
    CHECK(iolink_isdu_direct_param_page1_octet(&dev.dll.isdu, 6U) == 0x08U);
    CHECK(rig_init(&dev, &link, 0U));
    CHECK(dev.dll.od_len == 2U);
    CHECK(iolink_isdu_direct_param_page1_octet(&dev.dll.isdu, 3U) == 0x0BU);
    link.config.stack.od_len = 3U;
    CHECK(iolink_device_init(&dev, &link.config) < 0);
    link.config.stack.od_len = 1U;
    link.config.stack.pd_in_len = 33U;
    CHECK(iolink_device_init(&dev, &link.config) < 0);
    link.config.stack.pd_in_len = 2U;
    CHECK(iolink_device_init(&dev, &link.config) < 0); /* OD1 short PD needs code0. */
    link.config.stack.pd_in_len = 0U;
    link.config.stack.pd_out_len = 3U;
    CHECK(iolink_device_init(&dev, &link.config) == 0);
    link.config.stack.od_len = 2U;
    link.config.stack.pd_out_len = 0U;
    CHECK(iolink_device_init(&dev, &link.config) < 0); /* TYPE_2_V requires PD. */
    return true;
}

static bool current_cycle_340_frames(void)
{
    iolink_device_ctx_t dev;
    rig_link_t link;
    CHECK(rig_init(&dev, &link, 1U));
    for (unsigned n = 0; n < 340U; ++n) {
        uint8_t req[4];
        const uint8_t selector = (uint8_t) ((n * 13U + 7U) % 34U);
        rig_pd_request(req, selector, false);
        rig_exchange(&dev, &link, req, 3U);
        CHECK(iolink_device_get_state(&dev) == IOLINK_DLL_STATE_OPERATE);
        CHECK(link.tx_len == 34U); /* OD1 + PDin32 + CKS. */
        CHECK(memcmp(link.tx + 1U, link.blocks[selector], 32U) == 0);
        CHECK(memcmp(dev.dll.pd_in, link.blocks[selector], 32U) == 0);
        CHECK((link.tx[33] & 0x40U) == 0U);
        CHECK(link.select_calls == n + 1U);
    }
    return true;
}

static bool checksum_and_fragment_guards(void)
{
    iolink_device_ctx_t dev;
    rig_link_t link;
    CHECK(rig_init(&dev, &link, 1U));
    uint8_t req[4];
    rig_pd_request(req, 11U, false);
    rig_exchange(&dev, &link, req, 2U);
    CHECK(link.select_calls == 0U && link.tx_len == 0U);
    rig_exchange(&dev, &link, req + 2U, 1U);
    CHECK(link.select_calls == 1U && link.tx_len == 34U);
    CHECK(memcmp(link.tx + 1U, link.blocks[11], 32U) == 0);
    rig_pd_request(req, 22U, false);
    req[1] ^= 1U;
    rig_exchange(&dev, &link, req, 3U);
    CHECK(link.select_calls == 1U && link.tx_len == 0U);
    CHECK(dev.dll.crc_errors == 1U);
    rig_pd_request(req, 5U, false);
    rig_exchange(&dev, &link, req, 3U);
    CHECK(link.select_calls == 2U && link.tx_len == 34U);
    CHECK(memcmp(link.tx + 1U, link.blocks[5], 32U) == 0);
    return true;
}

static bool write_cycle_and_invalid_selector(void)
{
    iolink_device_ctx_t dev;
    rig_link_t link;
    CHECK(rig_init(&dev, &link, 1U));
    uint8_t req[4];
    rig_pd_request(req, 31U, true);
    rig_exchange(&dev, &link, req, 4U);
    CHECK(link.tx_len == 33U); /* WRITE has no returned OD. */
    CHECK(memcmp(link.tx, link.blocks[31], 32U) == 0);
    rig_pd_request(req, 250U, false);
    rig_exchange(&dev, &link, req, 3U);
    CHECK(link.tx_len == 34U && (link.tx[33] & 0x40U) != 0U);
    CHECK(memcmp(link.tx + 1U, link.blocks[31], 32U) == 0);
    return true;
}

typedef struct {
    unsigned calls;
    uint16_t index;
    uint8_t subindex;
    size_t read_len, write_len, blob_offset;
    uint8_t written[232];
    const uint8_t* blob;
} rig_vendor_t;

static uint8_t rig_vendor(void* user, uint16_t index, uint8_t subindex, bool write,
                           const uint8_t* input, size_t length, uint8_t* output,
                           size_t* output_length)
{
    rig_vendor_t* vendor = user;
    vendor->calls++;
    vendor->index = index;
    vendor->subindex = subindex;
    if (index == 0xFFFFU) return IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
    if (write) {
        if (length > sizeof(vendor->written)) return IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        memcpy(vendor->written, input, length);
        vendor->write_len = length;
        *output_length = 0U;
    }
    else {
        const size_t writable = vendor->read_len < *output_length ? vendor->read_len : *output_length;
        for (size_t i = 0; i < writable; ++i) {
            output[i] = vendor->blob != NULL ? vendor->blob[vendor->blob_offset + i]
                                               : (uint8_t) ((i + subindex * 17U) ^ 0xA5U);
        }
        *output_length = vendor->read_len;
    }
    return IOLINK_ISDU_ERROR_NONE;
}

static void rig_isdu_init(iolink_isdu_ctx_t* ctx, rig_vendor_t* vendor)
{
    iolink_isdu_init(ctx);
    memset(vendor, 0, sizeof(*vendor));
    ctx->vendor_service = rig_vendor;
    ctx->vendor_user = vendor;
}

static void rig_isdu_feed(iolink_isdu_ctx_t* ctx, const uint8_t* request, size_t len, bool duplicate)
{
    for (size_t i = 0U; i < len; ++i) {
        const uint8_t flow = i == 0U ? IOLINK_FLOWCTRL_START : (uint8_t) (i & 0x0FU);
        iolink_isdu_od_write(ctx, flow, request + i, 1U);
        if (duplicate && i != 0U && (i == 2U || i == 19U || i == len - 1U)) {
            iolink_isdu_od_write(ctx, flow, request + i, 1U);
        }
    }
}

static void rig_isdu_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    uint8_t req[5] = {0xB5U, (uint8_t) (index >> 8), (uint8_t) index, subindex, 0U};
    req[4] = (uint8_t) (req[0] ^ req[1] ^ req[2] ^ req[3]);
    rig_isdu_feed(ctx, req, sizeof(req), false);
}

static bool rig_isdu_response(iolink_isdu_ctx_t* ctx, uint8_t* response,
                               size_t* total, bool duplicate)
{
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, response, 1U);
    size_t expected = response[0] & 0x0FU;
    CHECK(expected != 0U);
    if (expected == 1U) {
        iolink_isdu_od_read(ctx, 1U, response + 1U, 1U);
        expected = response[1];
        CHECK(expected >= 17U && expected <= 238U);
    }
    const size_t start = (response[0] & 0x0FU) == 1U ? 2U : 1U;
    for (size_t i = start; i < expected; ++i) {
        const uint8_t flow = (uint8_t) (i & 0x0FU);
        iolink_isdu_od_read(ctx, flow, response + i, 1U);
        if (duplicate && (i == 2U || i == 17U || i == expected - 1U)) {
            uint8_t retry = 0U;
            iolink_isdu_od_read(ctx, flow, &retry, 1U);
            CHECK(retry == response[i]);
        }
    }
    uint8_t xor_sum = 0U;
    for (size_t i = 0; i < expected; ++i) xor_sum ^= response[i];
    CHECK(xor_sum == 0U);
    *total = expected;
    return true;
}

static bool extended_indices(void)
{
    iolink_isdu_ctx_t ctx;
    rig_vendor_t vendor;
    rig_isdu_init(&ctx, &vendor);
    vendor.read_len = 2U;
    rig_isdu_read_request(&ctx, 0x00FFU, 0U);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 0U && ctx.response_buf[0] == 0x80U && ctx.response_buf[1] == 0x11U);
    rig_isdu_read_request(&ctx, 0x0100U, 7U);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 1U && vendor.index == 0x0100U && vendor.subindex == 7U);
    CHECK(ctx.response_len == 2U && ctx.vendor_positive);
    rig_isdu_read_request(&ctx, 0xFFFFU, 0U);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 2U && vendor.index == 0xFFFFU);
    CHECK(!ctx.vendor_positive && ctx.response_buf[1] == 0x11U);
    return true;
}

static bool read_boundary_232_233_and_replay(void)
{
    iolink_isdu_ctx_t ctx;
    rig_vendor_t vendor;
    rig_isdu_init(&ctx, &vendor);
    vendor.read_len = 232U;
    rig_isdu_read_request(&ctx, 0x0100U, 0U);
    iolink_isdu_process(&ctx);
    CHECK(ctx.response_len == 232U && ctx.resp_total == 235U);
    uint8_t response[238];
    size_t length;
    CHECK(rig_isdu_response(&ctx, response, &length, true));
    CHECK(length == 235U && response[0] == 0xD1U && response[1] == 235U);
    for (size_t i = 0; i < 232U; ++i) CHECK(response[i + 2U] == (uint8_t) (i ^ 0xA5U));
    vendor.read_len = 233U;
    rig_isdu_read_request(&ctx, 0x0100U, 0U);
    iolink_isdu_process(&ctx);
    CHECK(rig_isdu_response(&ctx, response, &length, true));
    CHECK(length == 4U && response[0] == 0xC4U && response[1] == 0x80U);
    return true;
}

static bool write_boundary_and_count_wrap(void)
{
    iolink_isdu_ctx_t ctx;
    rig_vendor_t vendor;
    rig_isdu_init(&ctx, &vendor);
    uint8_t req[239] = {0x31U, 238U, 1U, 0U, 0U};
    for (size_t i = 0; i < 232U; ++i) req[i + 5U] = (uint8_t) (i ^ 0x5AU);
    for (size_t i = 0; i < 237U; ++i) req[237] ^= req[i];
    rig_isdu_feed(&ctx, req, 238U, true);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 1U && vendor.write_len == 232U && vendor.index == 0x0100U);
    CHECK(memcmp(vendor.written, req + 5U, 232U) == 0);
    CHECK(ctx.resp_total == 2U && ctx.resp_buf[0] == 0x52U);
    /* A 233-byte preferred-index write fits under total 238 but exceeds the
       record bound. It must never reach the application. */
    memset(req, 0, sizeof(req));
    req[0] = 0x11U; req[1] = 237U; req[2] = 0x40U;
    for (size_t i = 0; i < 233U; ++i) req[i + 3U] = (uint8_t) i;
    for (size_t i = 0; i < 236U; ++i) req[236] ^= req[i];
    rig_isdu_feed(&ctx, req, 237U, false);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 1U && ctx.response_buf[0] == 0x80U);
    /* Extended-index write with 233 data bytes declares invalid total239. */
    const uint8_t invalid_ext[] = {0x31U, 239U};
    rig_isdu_feed(&ctx, invalid_ext, sizeof(invalid_ext), false);
    CHECK(ctx.state == ISDU_STATE_IDLE && vendor.calls == 1U);
    return true;
}

static bool busy_abort_and_bad_count(void)
{
    iolink_isdu_ctx_t ctx;
    rig_vendor_t vendor;
    rig_isdu_init(&ctx, &vendor);
    vendor.read_len = 20U;
    rig_isdu_read_request(&ctx, 0x0100U, 0U);
    CHECK(ctx.state == ISDU_STATE_SERVICE_EXECUTE && vendor.calls == 0U);
    uint8_t reply;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &reply, 1U);
    CHECK(reply == 0x01U);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 1U);
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &reply, 1U);
    CHECK(reply == 0xD1U);
    iolink_isdu_od_read(&ctx, 3U, &reply, 1U); /* expected COUNT1 */
    CHECK(reply == 0U && ctx.state == ISDU_STATE_IDLE);
    rig_isdu_read_request(&ctx, 0x0100U, 0U);
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_ABORT, &reply, 1U);
    iolink_isdu_process(&ctx);
    CHECK(ctx.state == ISDU_STATE_IDLE && vendor.calls == 1U);
    const uint8_t controls[] = {
        IOLINK_FLOWCTRL_ABORT, IOLINK_FLOWCTRL_IDLE, IOLINK_FLOWCTRL_IDLE2
    };
    for (size_t i = 0U; i < sizeof(controls); ++i) {
        rig_isdu_read_request(&ctx, 0x0100U, 0U);
        CHECK(ctx.state == ISDU_STATE_SERVICE_EXECUTE);
        iolink_isdu_od_read(&ctx, controls[i], NULL, 0U);
        iolink_isdu_process(&ctx);
        CHECK(ctx.state == ISDU_STATE_IDLE && vendor.calls == 1U);
    }
    const uint8_t bad[] = {0xB5U, 1U, 0U, 0U, 0U};
    rig_isdu_feed(&ctx, bad, sizeof(bad), false);
    iolink_isdu_process(&ctx);
    CHECK(vendor.calls == 1U && ctx.resp_buf[0] == 0xC4U);
    return true;
}

static bool one_kib_in_five_records(void)
{
    iolink_isdu_ctx_t ctx;
    rig_vendor_t vendor;
    uint8_t original[1024], reconstructed[1024], response[238];
    for (size_t i = 0; i < sizeof(original); ++i) original[i] = (uint8_t) (i * 43U + (i >> 8U));
    rig_isdu_init(&ctx, &vendor);
    vendor.blob = original;
    size_t offset = 0;
    for (uint8_t record = 0; record < 5U; ++record) {
        const size_t remaining = sizeof(original) - offset;
        const size_t amount = remaining > 232U ? 232U : remaining;
        vendor.read_len = amount;
        vendor.blob_offset = offset;
        rig_isdu_read_request(&ctx, 0x0100U, record);
        iolink_isdu_process(&ctx);
        size_t length;
        CHECK(rig_isdu_response(&ctx, response, &length, true));
        CHECK(length == amount + 3U && response[0] == 0xD1U);
        memcpy(reconstructed + offset, response + 2U, amount);
        offset += amount;
    }
    CHECK(offset == sizeof(original) && vendor.calls == 5U);
    CHECK(memcmp(original, reconstructed, sizeof(original)) == 0);
    return true;
}

static bool device_local_identity_and_min_cycle(void)
{
    iolink_device_ctx_t first, second;
    rig_link_t first_link, second_link;
    const iolink_device_info_t first_info = {
        .vendor_name = "RigA", .vendor_id = 0x1234U, .device_id = 0x123456U,
        .min_cycle_time = 10U
    };
    const iolink_device_info_t second_info = {
        .vendor_name = "RigB", .vendor_id = 0x5678U, .device_id = 0x654321U,
        .min_cycle_time = 20U
    };
    const iolink_device_info_t legacy_before = *iolink_device_info_get();
    CHECK(rig_init(&first, &first_link, 1U));
    CHECK(rig_init(&second, &second_link, 1U));
    first_link.config.device_info = &first_info;
    first_link.config.stack.min_cycle_time = 30U;
    second_link.config.device_info = &second_info;
    second_link.config.stack.min_cycle_time = 150U;
    CHECK(iolink_device_init(&first, &first_link.config) == 0);
    CHECK(iolink_device_init(&second, &second_link.config) == 0);
    CHECK(first.dll.min_cycle_time_us == 3000U);
    CHECK(second.dll.min_cycle_time_us == 15000U);
    uint8_t request[2] = {0xA2U, 0U};
    request[1] = iolink_checksum6(request, sizeof(request));
    rig_exchange(&first, &first_link, request, sizeof(request));
    CHECK(first_link.tx_len == 2U && first_link.tx[0] == 0x1EU);
    rig_exchange(&second, &second_link, request, sizeof(request));
    CHECK(second_link.tx_len == 2U && second_link.tx[0] == 0x56U);
    CHECK(iolink_isdu_direct_param_page1_octet(&first.dll.isdu, 7U) == 0x12U);
    CHECK(iolink_isdu_direct_param_page1_octet(&second.dll.isdu, 7U) == 0x56U);
    CHECK(iolink_isdu_direct_param_page1_octet(&first.dll.isdu, 8U) == 0x34U);
    CHECK(iolink_isdu_direct_param_page1_octet(&second.dll.isdu, 8U) == 0x78U);
    CHECK(iolink_isdu_direct_param_page1_octet(&first.dll.isdu, 9U) == 0x12U);
    CHECK(iolink_isdu_direct_param_page1_octet(&second.dll.isdu, 9U) == 0x65U);
    rig_isdu_read_request(&first.dll.isdu, 0x0010U, 0U);
    iolink_isdu_process(&first.dll.isdu);
    CHECK(first.dll.isdu.response_len == 4U);
    CHECK(memcmp(first.dll.isdu.response_buf, "RigA", 4U) == 0);
    rig_isdu_read_request(&second.dll.isdu, 0x0010U, 0U);
    iolink_isdu_process(&second.dll.isdu);
    CHECK(second.dll.isdu.response_len == 4U);
    CHECK(memcmp(second.dll.isdu.response_buf, "RigB", 4U) == 0);
    CHECK(iolink_isdu_direct_param_page1_octet(&first.dll.isdu, 2U) == 0x1EU);
    CHECK(first_info.min_cycle_time == 10U && second_info.min_cycle_time == 20U);
    iolink_isdu_ctx_t legacy;
    iolink_isdu_init(&legacy);
    CHECK(iolink_isdu_direct_param_page1_octet(&legacy, 2U) ==
          iolink_cycle_time_encode(legacy_before.min_cycle_time));
    CHECK(iolink_isdu_direct_param_page1_octet(&legacy, 7U) ==
          (uint8_t) (legacy_before.vendor_id >> 8));
    CHECK(iolink_device_info_get()->min_cycle_time == legacy_before.min_cycle_time);
    CHECK(iolink_device_info_get()->vendor_id == legacy_before.vendor_id);
    return true;
}

unsigned iolink_protocol_rig_run(void (*emit)(const char*))
{
    const struct { const char* name; bool (*run)(void); } cases[] = {
        {"exact_code4_profile_and_legacy_default", exact_profile},
        {"device_local_identity_and_min_cycle", device_local_identity_and_min_cycle},
        {"current_cycle_340_distinct_requests", current_cycle_340_frames},
        {"checksum_and_fragment_guards", checksum_and_fragment_guards},
        {"write_cycle_and_invalid_selector", write_cycle_and_invalid_selector},
        {"extended_indices_no_alias", extended_indices},
        {"ISDU_read_232_233_and_COUNT_replay", read_boundary_232_233_and_replay},
        {"ISDU_write_232_233_and_COUNT_wrap", write_boundary_and_count_wrap},
        {"ISDU_pending_busy_abort_and_bad_COUNT", busy_abort_and_bad_count},
        {"ISDU_1024_bytes_five_records", one_kib_in_five_records},
    };
    unsigned failed = 0U;
    char line[180];
    if (emit != NULL) emit("IOLINKI_PROTOCOL_RIG logical-byte tests; no electrical timing claim");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assertion_line = 0U;
        const bool passed = cases[i].run();
        if (!passed) ++failed;
        snprintf(line, sizeof(line), "%s %s assertion_line=%u", passed ? "PASS" : "FAIL",
                 cases[i].name, assertion_line);
        if (emit != NULL) emit(line);
    }
    snprintf(line, sizeof(line), "IOLINKI_PROTOCOL_RIG_RESULT cases=%u failed=%u",
             (unsigned) (sizeof(cases) / sizeof(cases[0])), failed);
    if (emit != NULL) emit(line);
    return failed;
}

#ifdef IOLINK_RIG_HOST_MAIN
uint64_t iolink_time_get_us(void) { return 1000U; }
uint32_t iolink_time_get_ms(void) { return 1U; }
static void host_emit(const char* line) { puts(line); }
int main(void) { return (int) iolink_protocol_rig_run(host_emit); }
#endif
