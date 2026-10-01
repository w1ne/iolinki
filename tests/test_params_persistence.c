/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int write_result, writes;
int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    ++writes;
    return write_result;
}
int main(void)
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_device_info_ctx_init(&info, NULL);
    iolink_params_ctx_init(&params, &info);
    const uint16_t indices[] = {IOLINK_IDX_APPLICATION_TAG, IOLINK_IDX_FUNCTION_TAG,
                                IOLINK_IDX_LOCATION_TAG};
    for (unsigned i = 0; i < 3; ++i) {
        const uint8_t original[] = "original";
        const uint8_t replacement[] = "replacement";
        uint8_t result[32];
        write_result = 0;
        assert(iolink_params_ctx_set(&params, indices[i], 0, original, 8, true) == 0);
        int prior_writes = writes;
        write_result = -1;
        assert(iolink_params_ctx_set(&params, indices[i], 0, replacement, 11, true) == -1);
        assert(writes == prior_writes + 1);
        assert(iolink_params_ctx_get(&params, indices[i], 0, result, sizeof(result)) == 8);
        assert(memcmp(result, original, 8) == 0);
        assert(iolink_params_ctx_set(&params, indices[i], 0, replacement, 11, false) == 0);
        assert(writes == prior_writes + 1);
        assert(iolink_params_ctx_get(&params, indices[i], 0, result, sizeof(result)) == 11);
        assert(memcmp(result, replacement, 11) == 0);
    }
    /* Real byte transport must produce Write Response (-), not the old 52 52
     * positive acknowledgement, when persistent tag storage rejects the write. */
    const uint8_t requests[3][4] = {
        {0x14U, 0x18U, 0x78U, 0x74U}, /* ApplicationTag "x" */
        {0x14U, 0x19U, 0x78U, 0x75U}, /* FunctionTag "x" */
        {0x14U, 0x1AU, 0x78U, 0x76U}, /* LocationTag "x" */
    };
    const uint8_t expected[] = {0x44U, 0x80U, 0x11U, 0xD5U};
    for (unsigned tag = 0; tag < 3; ++tag) {
        iolink_isdu_ctx_t isdu;
        iolink_isdu_init(&isdu);
        isdu.params_ctx = &params;
        write_result = -1;
        int prior_writes = writes;
        for (unsigned i = 0; i < 4; ++i) {
            iolink_isdu_od_write(&isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                                 &requests[tag][i], 1);
        }
        iolink_isdu_process(&isdu);
        uint8_t response[4];
        for (unsigned i = 0; i < sizeof(response); ++i) {
            iolink_isdu_od_read(&isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, &response[i],
                                1);
        }
        assert(writes == prior_writes + 1);
        assert(memcmp(response, expected, sizeof(expected)) == 0);
        uint8_t retained[32];
        assert(iolink_params_ctx_get(&params, indices[tag], 0, retained, sizeof(retained)) == 11);
        assert(memcmp(retained, "replacement", 11) == 0);
    }
    puts("Persistent tag failures preserve RAM/device info; volatile writes still work");
}
