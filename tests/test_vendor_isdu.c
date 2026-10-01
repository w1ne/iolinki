/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include <assert.h>
#include <string.h>

static uint8_t parameter(void* user, uint16_t index, uint8_t subindex, bool write,
                         const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    unsigned* calls = user;
    (*calls)++;
    assert(index == 0x0100 && subindex == 0 && !write && length == 0);
    (void) input;
    assert(*capacity >= 2);
    output[0] = 0x80;
    output[1] = 0x34;
    *capacity = 2;
    return 0;
}

static uint8_t too_long(void* user, uint16_t index, uint8_t subindex, bool write,
                        const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) write;
    (void) input;
    (void) length;
    (void) output;
    *capacity = 999;
    return 0;
}

int main(void)
{
    iolink_isdu_ctx_t ctx;
    unsigned calls = 0;
    iolink_isdu_init(&ctx);
    ctx.vendor_service = parameter;
    ctx.vendor_user = &calls;
    /* 16-bit indexed read, subindex zero, independently XOR-checksummed. */
    const uint8_t request[] = {0xB5, 0x01, 0x00, 0x00, 0xB4};
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(&ctx);
    assert(calls == 1);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80 && ctx.response_buf[1] == 0x34);
    uint8_t octet;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1);
    assert(octet == 0xD4); /* 0x80xx is legitimate data, not an error marker. */
    const uint8_t malformed[] = {0xB5, 0x01, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < sizeof(malformed); i++) {
        iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, malformed + i, 1);
    }
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1);
    assert(octet == 0xC4); /* Bad CHKPDU after positive vendor data stays negative. */
    ctx.header.index = 0x0010;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(calls == 1); /* Mandatory indices cannot be overridden. */
    ctx.header.index = 0x0100;
    ctx.vendor_service = too_long;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
    assert(!ctx.vendor_positive);
    /* A missing callback retains the original unknown-index error. */
    ctx.vendor_service = NULL;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
    iolink_isdu_ctx_t other;
    unsigned other_calls = 0;
    iolink_isdu_init(&other);
    other.vendor_service = parameter;
    other.vendor_user = &other_calls;
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(&other, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(&other);
    assert(other_calls == 1 && calls == 1); /* Application ownership is per device. */
    return 0;
}
