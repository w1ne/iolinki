/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/frame.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include <string.h>

int iolink_frame_encode_type0(uint8_t mc, uint8_t* out, size_t out_size)
{
    if ((out == NULL) || (out_size < IOLINK_M_SEQ_TYPE0_LEN)) {
        return -1;
    }

    out[0] = mc;
    out[1] = iolink_checksum_ck(mc, 0U);

    return (int) IOLINK_M_SEQ_TYPE0_LEN;
}

int iolink_frame_encode_type0_write(uint8_t mc, uint8_t od, uint8_t* out, size_t out_size)
{
    if ((out == NULL) || (out_size < IOLINK_M_SEQ_MIN_LEN)) {
        return -1;
    }

    /* Type-0 write frame (MC + one OD data octet + CK). The trailing checksum is
       the 6-bit M-sequence CRC over the preceding octets, matching how the
       device DLL verifies any request longer than the 2-octet Type-0 read. */
    out[0] = mc;
    out[1] = od;
    out[2] = iolink_crc6(out, 2U);

    return (int) IOLINK_M_SEQ_MIN_LEN;
}

int iolink_frame_encode_type1_cycle(const uint8_t* pd_out, uint8_t pd_out_len, uint8_t od_len,
                                    uint8_t* out, size_t out_size)
{
    size_t pos = 0U;
    const size_t frame_len = (size_t) IOLINK_M_SEQ_HEADER_LEN + pd_out_len + od_len + 1U;

    if ((out == NULL) || ((pd_out == NULL) && (pd_out_len > 0U)) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE) || (od_len == 0U) || (od_len > IOLINK_OD_MAX_SIZE) ||
        (out_size < frame_len)) {
        return -1;
    }

    out[pos++] = 0U;
    out[pos++] = 0U;

    if (pd_out_len > 0U) {
        memcpy(&out[pos], pd_out, pd_out_len);
        pos += pd_out_len;
    }

    /* od_len is guaranteed non-zero by the guard above. */
    memset(&out[pos], 0, od_len);
    pos += od_len;

    out[pos] = iolink_crc6(out, (uint8_t) pos);

    return (int) frame_len;
}

int iolink_frame_decode_operate_response(const uint8_t* frame, size_t frame_len, uint8_t pd_in_len,
                                         uint8_t od_len, iolink_frame_operate_response_t* out)
{
    size_t pos = 0U;
    const size_t expected_len = 1U + pd_in_len + od_len + 1U;

    if ((frame == NULL) || (out == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) || (od_len == 0U) ||
        (od_len > IOLINK_OD_MAX_SIZE) || (frame_len != expected_len)) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    out->status = frame[pos++];
    out->pd_valid = ((out->status & IOLINK_OD_STATUS_PD_VALID) != 0U);
    out->event_pending = ((out->status & IOLINK_OD_STATUS_EVENT) != 0U);
    out->checksum_ok = (iolink_crc6(frame, (uint8_t) (frame_len - 1U)) == frame[frame_len - 1U]);

    if (pd_in_len > 0U) {
        memcpy(out->pd, &frame[pos], pd_in_len);
        out->pd_len = pd_in_len;
        pos += pd_in_len;
    }

    /* od_len is guaranteed non-zero by the guard above. */
    memcpy(out->od, &frame[pos], od_len);
    out->od_len = od_len;

    return 0;
}
