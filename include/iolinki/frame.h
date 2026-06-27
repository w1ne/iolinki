/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_FRAME_H
#define IOLINK_FRAME_H

#include "iolinki/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file frame.h
 * @brief Shared IO-Link frame encoding and decoding helpers.
 */

typedef struct
{
    uint8_t status;
    bool pd_valid;
    bool event_pending;
    bool checksum_ok;
    uint8_t pd[IOLINK_PD_IN_MAX_SIZE];
    uint8_t pd_len;
    uint8_t od[IOLINK_OD_MAX_SIZE];
    uint8_t od_len;
} iolink_frame_operate_response_t;

int iolink_frame_encode_type0(uint8_t mc, uint8_t* out, size_t out_size);
int iolink_frame_encode_type1_cycle(const uint8_t* pd_out, uint8_t pd_out_len, uint8_t od_len,
                                    uint8_t* out, size_t out_size);
int iolink_frame_decode_operate_response(const uint8_t* frame, size_t frame_len, uint8_t pd_in_len,
                                         uint8_t od_len, iolink_frame_operate_response_t* out);

#endif /* IOLINK_FRAME_H */
