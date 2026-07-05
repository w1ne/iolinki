/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stdint.h>
#include "iolinki/dll.h"

/**
 * @file dll_internal.h
 * @brief Internal DLL definitions and structures
 */

#include "iolinki/protocol.h"

#define IOLINK_M_SEQ_TYPE1_LEN(pd_len)                         \
    (4 + (pd_len)) /* MC(8) + CKT(8) + PD(var) + OD(8) + CK(8) \
                    */
#define IOLINK_M_SEQ_TYPE2_LEN(pd_len, od_len) \
    (3 + (pd_len) + (od_len)) /* MC(8) + CKT(8) + PD(var) + OD(var) + CK(8) */

/** @brief Decoded 2-octet Type-0 M-sequence (master command + checksum). */
typedef struct
{
    uint8_t master_command; /**< Master command octet (MC). */
    uint8_t ck;             /**< Checksum octet (CK). */
} iolink_m_seq_type0_t;

/** @brief On-request data (OD) octet paired with a validity flag. */
typedef struct
{
    uint8_t od_data; /**< On-request data octet. */
    uint8_t valid;   /**< Non-zero when @ref od_data holds a valid value. */
} iolink_od_req_t;

#endif  // IOLINK_DLL_INTERNAL_H
