/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PROTOCOL_H
#define IOLINK_PROTOCOL_H

/**
 * @file protocol.h
 * @brief IO-Link Protocol Constants and Definitions (Spec V1.1.2)
 */

/* M-Sequence Type Lengths */
#define IOLINK_M_SEQ_TYPE0_LEN           2U
#define IOLINK_M_SEQ_HEADER_LEN          2U  /* MC + CKT */
#define IOLINK_M_SEQ_MIN_LEN             3U  /* MC + CKT + CK (Type 1/2) */

/* Master Command (MC) Definitions */
#define IOLINK_MC_RW_MASK                0x80U
#define IOLINK_MC_COMM_CHANNEL_MASK      0x60U
#define IOLINK_MC_ADDR_MASK              0x1FU

#define IOLINK_MC_TRANSITION_COMMAND     0x0FU

/* ISDU Control Byte Bits */
#define IOLINK_ISDU_CTRL_START           0x80U
#define IOLINK_ISDU_CTRL_LAST            0x40U
#define IOLINK_ISDU_CTRL_SEQ_MASK        0x3FU

/* ISDU Service IDs */
#define IOLINK_ISDU_SERVICE_READ         0x09U
#define IOLINK_ISDU_SERVICE_WRITE        0x0AU

/* Mandatory ISDU Indices */
#define IOLINK_IDX_DIRECT_PARAMETERS_1   0x0000U
#define IOLINK_IDX_DIRECT_PARAMETERS_2   0x0001U
#define IOLINK_IDX_SYSTEM_COMMAND        0x0002U
#define IOLINK_IDX_VENDOR_ID             0x000AU
#define IOLINK_IDX_DEVICE_ID             0x000BU
#define IOLINK_IDX_DEVICE_ACCESS_LOCKS   0x000CU
#define IOLINK_IDX_PROFILE_CHARACTERISTIC 0x000DU
#define IOLINK_IDX_VENDOR_NAME           0x0010U
#define IOLINK_IDX_VENDOR_TEXT           0x0011U
#define IOLINK_IDX_PRODUCT_NAME          0x0012U
#define IOLINK_IDX_PRODUCT_ID            0x0013U
#define IOLINK_IDX_PRODUCT_TEXT          0x0014U
#define IOLINK_IDX_SERIAL_NUMBER         0x0015U
#define IOLINK_IDX_HARDWARE_REVISION     0x0016U
#define IOLINK_IDX_FIRMWARE_REVISION     0x0017U
#define IOLINK_IDX_APPLICATION_TAG       0x0018U
#define IOLINK_IDX_DEVICE_STATUS         0x001BU
#define IOLINK_IDX_DETAILED_DEVICE_STATUS 0x001CU
#define IOLINK_IDX_REVISION_ID           0x001EU
#define IOLINK_IDX_MIN_CYCLE_TIME        0x0024U

/* System Commands */
#define IOLINK_CMD_RESTORE_FACTORY_SETTINGS 0x82U

/* ISDU Error Codes (0x80xx) */
#define IOLINK_ISDU_ERROR_NONE           0x00U
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL 0x11U
#define IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL 0x12U
#define IOLINK_ISDU_ERROR_BUSY           0x30U
#define IOLINK_ISDU_ERROR_WRITE_PROTECTED 0x33U

/* Event Constants */
#define IOLINK_EVENT_BIT_STATUS          0x80U  /* MSB of status byte in Type 1/2 */

/* Standard IO-Link Event Codes (0x1XXX–0x8XXX) */
#define IOLINK_EVENT_CODE_COMM_ERR_GENERAL    0x1800U
#define IOLINK_EVENT_CODE_COMM_ERR_FRAMING    0x1801U
#define IOLINK_EVENT_CODE_COMM_ERR_CRC        0x1803U

/* On-Request Data (OD) Constants */
#define IOLINK_OD_LEN_8BIT               1U     /* 1-byte OD (Type 1_x) */
#define IOLINK_OD_LEN_16BIT              2U     /* 2-byte OD (Type 2_x) */
#define IOLINK_OD_LEN_32BIT              4U     /* 4-byte OD (Type 2_V extended) */

/* OD Status Byte Bit Definitions (First byte of OD) */
#define IOLINK_OD_STATUS_EVENT           0x80U  /* Bit 7: Event present */
#define IOLINK_OD_STATUS_RESERVED        0x40U  /* Bit 6: Reserved */
#define IOLINK_OD_STATUS_PD_VALID        0x20U  /* Bit 5: PD_In valid */
#define IOLINK_OD_STATUS_DEVICE_MASK     0x1FU  /* Bits 4-0: Device status flags */

/* Device Status Flags (lower 5 bits of OD status) */
#define IOLINK_DEVICE_STATUS_OK          0x00U  /* Device operating normally */
#define IOLINK_DEVICE_STATUS_MAINTENANCE 0x01U  /* Maintenance required */
#define IOLINK_DEVICE_STATUS_OUT_OF_SPEC 0x02U  /* Out of specification */
#define IOLINK_DEVICE_STATUS_FAILURE     0x03U  /* Functional failure */

#endif /* IOLINK_PROTOCOL_H */
