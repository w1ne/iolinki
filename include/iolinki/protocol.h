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

/**
 * @defgroup iolinki_protocol IO-Link Protocol Constants
 * @brief M-sequence, Master Command, ISDU, index, command and status constants.
 *
 * The constants are split into the sub-modules below, each mirroring a table
 * or field layout from the IO-Link Interface Specification.
 */

/**
 * @defgroup iolinki_protocol_mseq M-Sequence Type Lengths
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_M_SEQ_TYPE0_LEN 2U  /**< Type 0 M-sequence length in bytes. */
#define IOLINK_M_SEQ_HEADER_LEN 2U /**< M-sequence header length: MC + CKT. */
#define IOLINK_M_SEQ_MIN_LEN 3U    /**< Minimum M-sequence length: MC + CKT + CK (Type 1/2). */
/** @} */

/**
 * @defgroup iolinki_protocol_mc Master Command (MC) Definitions
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_MC_RW_MASK 0x80U           /**< Read/Write bit mask in the MC byte. */
#define IOLINK_MC_COMM_CHANNEL_MASK 0x60U /**< Communication channel field mask in the MC byte. */
#define IOLINK_MC_ADDR_MASK 0x1FU         /**< Address field mask in the MC byte. */

#define IOLINK_MC_TRANSITION_COMMAND 0x0FU /**< Address used for the transition command. */

/** @brief MasterCommand value written to Direct Parameter page address 0x00 (Table B.2). */
#define IOLINK_CMD_DEVICE_OPERATE 0x99U
/** @} */

/**
 * @defgroup iolinki_protocol_isdu_ctrl ISDU Control Byte Bits
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_ISDU_CTRL_START 0x80U    /**< Start-of-transfer flag. */
#define IOLINK_ISDU_CTRL_LAST 0x40U     /**< Last-segment flag. */
#define IOLINK_ISDU_CTRL_SEQ_MASK 0x3FU /**< Sequence counter mask. */
/** @} */

/**
 * @defgroup iolinki_protocol_isdu_service ISDU Service IDs
 * @ingroup iolinki_protocol
 * @brief I-Service nibble, IO-Link spec Table A.12 (16-bit Index + Subindex form).
 * @{
 */
#define IOLINK_ISDU_SERVICE_READ 0x0BU  /**< ISDU read service. */
#define IOLINK_ISDU_SERVICE_WRITE 0x03U /**< ISDU write service. */
/** @} */

/**
 * @defgroup iolinki_protocol_indices Mandatory ISDU Indices
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_IDX_DIRECT_PARAMETERS_1 0x0000U    /**< Direct Parameter page 1. */
#define IOLINK_IDX_DIRECT_PARAMETERS_2 0x0001U    /**< Direct Parameter page 2. */
#define IOLINK_IDX_SYSTEM_COMMAND 0x0002U         /**< System Command index. */
#define IOLINK_IDX_DATA_STORAGE 0x0003U           /**< Data Storage object (parameter image). */
#define IOLINK_IDX_VENDOR_ID 0x000AU              /**< Vendor ID index. */
#define IOLINK_IDX_DEVICE_ID 0x000BU              /**< Device ID index. */
#define IOLINK_IDX_DEVICE_ACCESS_LOCKS 0x000CU    /**< Device Access Locks index. */
#define IOLINK_IDX_PROFILE_CHARACTERISTIC 0x000DU /**< Profile characteristic index. */
#define IOLINK_IDX_VENDOR_NAME 0x0010U            /**< Vendor name index. */
#define IOLINK_IDX_VENDOR_TEXT 0x0011U            /**< Vendor text index. */
#define IOLINK_IDX_PRODUCT_NAME 0x0012U           /**< Product name index. */
#define IOLINK_IDX_PRODUCT_ID 0x0013U             /**< Product ID index. */
#define IOLINK_IDX_PRODUCT_TEXT 0x0014U           /**< Product text index. */
#define IOLINK_IDX_SERIAL_NUMBER 0x0015U          /**< Serial number index. */
#define IOLINK_IDX_HARDWARE_REVISION 0x0016U      /**< Hardware revision index. */
#define IOLINK_IDX_FIRMWARE_REVISION 0x0017U      /**< Firmware revision index. */
#define IOLINK_IDX_APPLICATION_TAG 0x0018U        /**< Application tag index. */
#define IOLINK_IDX_FUNCTION_TAG 0x0019U           /**< Function tag index. */
#define IOLINK_IDX_LOCATION_TAG 0x001AU           /**< Location tag index. */
#define IOLINK_IDX_DEVICE_STATUS 0x001BU          /**< Device status index. */
#define IOLINK_IDX_DETAILED_DEVICE_STATUS 0x001CU /**< Detailed device status index. */
#define IOLINK_IDX_PDIN_DESCRIPTOR 0x001DU        /**< Process Data In descriptor index. */
#define IOLINK_IDX_REVISION_ID 0x001EU            /**< Revision ID index. */
#define IOLINK_IDX_MIN_CYCLE_TIME 0x0024U         /**< Minimum cycle time index. */
#define IOLINK_IDX_ERROR_STATS 0x0025U            /**< Vendor-specific error statistics. */
/** @} */

/**
 * @defgroup iolinki_protocol_sys_cmd System Commands (Index 0x0002)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_CMD_PARAM_DOWNLOAD_START 0x05U /**< ParamDownloadStart command. */
#define IOLINK_CMD_PARAM_DOWNLOAD_END 0x06U   /**< ParamDownloadEnd command. */
#define IOLINK_CMD_PARAM_UPLOAD_START 0x07U   /**< ParamUploadStart command. */
#define IOLINK_CMD_PARAM_UPLOAD_END 0x08U     /**< ParamUploadEnd command. */
#define IOLINK_CMD_PARAM_DOWNLOAD_STORE 0x09U /**< ParamDownloadStore (V1.0 legacy or optional). */
/** @} */

/**
 * @defgroup iolinki_protocol_legacy_cmd Legacy / Non-Standard Commands (Deprecating)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_CMD_DEVICE_RESET 0x80U             /**< Device reset command. */
#define IOLINK_CMD_APPLICATION_RESET 0x81U        /**< Application reset command. */
#define IOLINK_CMD_RESTORE_FACTORY_SETTINGS 0x82U /**< Restore factory settings command. */
#define IOLINK_CMD_RESTORE_APP_DEFAULTS 0x83U     /**< Restore application defaults command. */
#define IOLINK_CMD_SET_COMM_MODE 0x84U            /**< Set communication mode command. */
#define IOLINK_CMD_PARAM_UPLOAD 0x95U             /**< Parameter upload command. */
#define IOLINK_CMD_PARAM_DOWNLOAD 0x96U           /**< Parameter download command. */
#define IOLINK_CMD_PARAM_BREAK 0x97U              /**< Parameter break command. */
/** @} */

/**
 * @defgroup iolinki_protocol_locks Device Access Locks (Index 0x000C)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_LOCK_PARAM 0x01U       /**< Bit 0: Parameter (write) access lock. */
#define IOLINK_LOCK_DS 0x02U          /**< Bit 1: Data Storage access lock. */
#define IOLINK_LOCK_LOCAL_PARAM 0x04U /**< Bit 2: Local parameterization lock. */
#define IOLINK_LOCK_LOCAL_UI 0x08U    /**< Bit 3: Local user interface lock. */
/** @} */

/**
 * @defgroup iolinki_protocol_isdu_err ISDU Error Codes (0x80xx)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_ISDU_ERROR_NONE 0x00U               /**< No error. */
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL 0x11U  /**< Requested service not available. */
#define IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL 0x12U /**< Requested subindex not available. */
#define IOLINK_ISDU_ERROR_BUSY 0x30U               /**< Device busy. */
#define IOLINK_ISDU_ERROR_WRITE_PROTECTED 0x33U    /**< Parameter is write-protected. */
#define IOLINK_ISDU_ERROR_PARAM_INCONSISTENT 0x40U /**< Parameter set inconsistent. */
#define IOLINK_ISDU_ERROR_SEGMENTATION 0x81U       /**< Segmentation error. */
/** @} */

/**
 * @defgroup iolinki_protocol_event Event Constants
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_EVENT_BIT_STATUS 0x80U /**< MSB of status byte in Type 1/2 (event flag). */
/** @} */

/**
 * @defgroup iolinki_protocol_event_codes Standard IO-Link Event Codes (0x1XXX-0x8XXX)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_EVENT_CODE_COMM_ERR_GENERAL 0x1800U /**< General communication error. */
#define IOLINK_EVENT_CODE_COMM_ERR_FRAMING 0x1801U /**< Framing communication error. */
#define IOLINK_EVENT_CODE_COMM_ERR_CRC 0x1803U     /**< CRC communication error. */
/** @} */

/**
 * @defgroup iolinki_protocol_od On-Request Data (OD) Constants
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_OD_LEN_8BIT 1U  /**< 1-byte OD (Type 1_x). */
#define IOLINK_OD_LEN_16BIT 2U /**< 2-byte OD (Type 2_x). */
#define IOLINK_OD_LEN_32BIT 4U /**< 4-byte OD (Type 2_V extended). */
/** @} */

/**
 * @defgroup iolinki_protocol_od_status OD Status Byte Bit Definitions (First byte of OD)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_OD_STATUS_EVENT 0x80U       /**< Bit 7: Event present. */
#define IOLINK_OD_STATUS_PD_TOGGLE 0x40U   /**< Bit 6: PD Toggle (consistency). */
#define IOLINK_OD_STATUS_PD_VALID 0x20U    /**< Bit 5: PD_In valid. */
#define IOLINK_OD_STATUS_DEVICE_MASK 0x1FU /**< Bits 4-0: Device status flags. */
/** @} */

/**
 * @defgroup iolinki_protocol_device_status Device Status Flags (lower 5 bits of OD status)
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_DEVICE_STATUS_OK 0x00U          /**< Device operating normally. */
#define IOLINK_DEVICE_STATUS_MAINTENANCE 0x01U /**< Maintenance required. */
#define IOLINK_DEVICE_STATUS_OUT_OF_SPEC 0x02U /**< Out of specification. */
#define IOLINK_DEVICE_STATUS_FAILURE 0x03U     /**< Functional failure. */
/** @} */

#endif /* IOLINK_PROTOCOL_H */
