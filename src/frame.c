/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file frame.c
 * @brief M-sequence frame encoding and decoding.
 * @ingroup iolinki_frame
 *
 * Builds Type-0/Type-1 request frames and decodes OPERATE-mode response
 * frames, computing and verifying the associated M-sequence checksums.
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
    out[1] = 0x00U; /* CKT bits 0-5 zeroed before the A.1.6 checksum */
    out[1] = iolink_checksum6(out, 2U);

    return (int) IOLINK_M_SEQ_TYPE0_LEN;
}

int iolink_frame_encode_type0_write(uint8_t mc, uint8_t od, uint8_t* out, size_t out_size)
{
    if ((out == NULL) || (out_size < IOLINK_M_SEQ_MIN_LEN)) {
        return -1;
    }

    /* Type-0 write frame (MC + one OD data octet + CK). The trailing checksum is
       the A.1.6 message checksum over the preceding octets. */
    out[0] = mc;
    out[1] = od;
    out[2] = iolink_checksum6(out, 2U);

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

    out[pos] = iolink_checksum6(out, pos);

    return (int) frame_len;
}

int iolink_frame_decode_operate_response(const uint8_t* frame, size_t frame_len, uint8_t pd_in_len,
                                         uint8_t od_len, iolink_frame_operate_response_t* out)
{
    size_t pos = 0U;
    const size_t expected_len = pd_in_len + od_len + 1U;

    if ((frame == NULL) || (out == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) || (od_len == 0U) ||
        (od_len > IOLINK_OD_MAX_SIZE) || (frame_len != expected_len)) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    /* A.1.5: the reply is [PD-in][OD] CKS with no leading status octet. The CKS
       octet carries the Event flag in bit 7, the PD-invalid flag in bit 6 (so
       PD is valid when bit 6 is clear) and the 6-bit checksum in bits 0-5. */
    const uint8_t cks = frame[frame_len - 1U];
    out->status = cks;
    out->event_pending = ((cks & 0x80U) != 0U);
    out->pd_valid = ((cks & 0x40U) == 0U);

    /* Zero the checksum bits before re-computing (A.1.6); the flag bits stay. */
    uint8_t msg[IOLINK_M_SEQ_HEADER_LEN + IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1U];
    if (frame_len > sizeof(msg)) {
        return -1;
    }
    (void) memcpy(msg, frame, frame_len);
    msg[frame_len - 1U] = (uint8_t) (msg[frame_len - 1U] & 0xC0U);
    out->checksum_ok = (iolink_checksum6(msg, frame_len) == (uint8_t) (cks & 0x3FU));

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
