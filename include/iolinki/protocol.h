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
 * @brief IO-Link Protocol Constants and Definitions (Interface Spec V1.1.5)
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

/** @brief Communication channel values (A.1.2, Table A.1). */
#define IOLINK_MC_CHANNEL_PROCESS 0x00U   /**< Process data channel. */
#define IOLINK_MC_CHANNEL_PAGE 0x20U      /**< Direct Parameter page channel. */
#define IOLINK_MC_CHANNEL_DIAGNOSIS 0x40U /**< Diagnosis (event memory) channel. */
#define IOLINK_MC_CHANNEL_ISDU 0x60U      /**< ISDU channel (FlowCTRL in the address bits). */

#define IOLINK_MC_TRANSITION_COMMAND 0x0FU /**< Address used for the transition command. */

/** @brief M-sequence type bits in CKT/CKS (A.1.3, Table A.3). */
#define IOLINK_MSEQ_TYPE_MASK 0xC0U     /**< Type field mask in the CKT/CKS octet. */
#define IOLINK_MSEQ_TYPE_0 0x00U        /**< Type 0. */
#define IOLINK_MSEQ_TYPE_1 0x40U        /**< Type 1. */
#define IOLINK_MSEQ_TYPE_2 0x80U        /**< Type 2. */
#define IOLINK_MSEQ_TYPE_RESERVED 0xC0U /**< Reserved type. */
#define IOLINK_MSEQ_CHECKSUM_MASK 0x3FU /**< Checksum field mask in the CKT/CKS octet. */

/** @brief ISDU FlowCTRL values carried in the ISDU channel address bits (7.3.6.2, Table 52). */
#define IOLINK_FLOWCTRL_START 0x10U      /**< Start of an ISDU I-Service. */
#define IOLINK_FLOWCTRL_IDLE 0x11U       /**< No request for ISDU transmission. */
#define IOLINK_FLOWCTRL_IDLE2 0x12U      /**< Reserved / no request. */
#define IOLINK_FLOWCTRL_ABORT 0x1FU      /**< Abort entire service. */
#define IOLINK_FLOWCTRL_COUNT_MASK 0x0FU /**< COUNT M-sequence counter mask. */

/** @brief MasterCommand value written to Direct Parameter page address 0x00 (Table B.2). */
#define IOLINK_CMD_FALLBACK 0x5AU          /**< Transition to SIO (T_FBD, Table 43). */
#define IOLINK_CMD_DEVICE_OPERATE 0x99U    /**< Switch to OPERATE; PDOUTINVALID (Table 55). */
#define IOLINK_CMD_PD_OUTPUT_OPERATE 0x98U /**< PD output valid (Table 55). */
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
#define IOLINK_ISDU_DATA_MAX_SIZE 232U  /**< Maximum application data in one ISDU record. */
/** @} */

/**
 * @defgroup iolinki_protocol_indices Standard ISDU Indices (Table B.8)
 * @ingroup iolinki_protocol
 * @brief Index assignment of Device parameters, IO-Link V1.1.5 Table B.8.
 *
 * VendorID and DeviceID have no ISDU Index of their own: Indices 0x0004 to
 * 0x000B are reserved. Both identifiers are read through Direct Parameter
 * page 1 (Index 0x0000, Table B.1 addresses 0x07 to 0x0B).
 * @{
 */
#define IOLINK_IDX_DIRECT_PARAMETERS_1 0x0000U    /**< Direct Parameter page 1 (R, M). */
#define IOLINK_IDX_DIRECT_PARAMETERS_2 0x0001U    /**< Direct Parameter page 2 (R/W, M). */
#define IOLINK_IDX_SYSTEM_COMMAND 0x0002U         /**< SystemCommand (W, C, B.2.2). */
#define IOLINK_IDX_DATA_STORAGE 0x0003U           /**< DataStorageIndex (R/W, M, B.2.3). */
#define IOLINK_DS_SUBIDX_COMMAND 0x01U            /**< 0x0003/1 DS_Command (W, Table B.10). */
#define IOLINK_DS_SUBIDX_STATE_PROPERTY 0x02U     /**< 0x0003/2 State_Property (R). */
#define IOLINK_DS_SUBIDX_STORAGE_SIZE 0x03U       /**< 0x0003/3 Data_Storage_Size (R, U32). */
#define IOLINK_DS_SUBIDX_PARAM_CHECKSUM 0x04U     /**< 0x0003/4 Parameter_Checksum (R, U32). */
#define IOLINK_DS_SUBIDX_INDEX_LIST 0x05U         /**< 0x0003/5 Index_List (R, Table B.11). */
#define IOLINK_IDX_DEVICE_ACCESS_LOCKS 0x000CU    /**< Device Access Locks (R/W, O, B.2.4). */
#define IOLINK_IDX_PROFILE_CHARACTERISTIC 0x000DU /**< ProfileCharacteristic (R, C, B.2.5). */
#define IOLINK_IDX_PD_INPUT_DESCRIPTOR 0x000EU    /**< PDInputDescriptor (R, C, B.2.6). */
#define IOLINK_IDX_PD_OUTPUT_DESCRIPTOR 0x000FU   /**< PDOutputDescriptor (R, C, B.2.7). */
#define IOLINK_IDX_VENDOR_NAME 0x0010U            /**< VendorName (R, M, max 64). */
#define IOLINK_IDX_VENDOR_TEXT 0x0011U            /**< VendorText (R, O, max 64). */
#define IOLINK_IDX_PRODUCT_NAME 0x0012U           /**< ProductName (R, M, max 64). */
#define IOLINK_IDX_PRODUCT_ID 0x0013U             /**< ProductID (R, O, max 64). */
#define IOLINK_IDX_PRODUCT_TEXT 0x0014U           /**< ProductText (R, O, max 64). */
#define IOLINK_IDX_SERIAL_NUMBER 0x0015U          /**< SerialNumber (R, O, max 16). */
#define IOLINK_IDX_HARDWARE_REVISION 0x0016U      /**< HardwareRevision (R, O, max 64). */
#define IOLINK_IDX_FIRMWARE_REVISION 0x0017U      /**< FirmwareRevision (R, O, max 64). */
#define IOLINK_IDX_APPLICATION_TAG 0x0018U        /**< ApplicationSpecificTag (R/W, O). */
#define IOLINK_IDX_FUNCTION_TAG 0x0019U           /**< FunctionTag (R/W, C). */
#define IOLINK_IDX_LOCATION_TAG 0x001AU           /**< LocationTag (R/W, C). */
#define IOLINK_IDX_PRODUCT_URI 0x001BU            /**< ProductURI (R, C, max 100). */
#define IOLINK_IDX_ERROR_COUNT 0x0020U            /**< ErrorCount (R, O, 2 octets). */
#define IOLINK_IDX_DEVICE_STATUS 0x0024U          /**< DeviceStatus (R, O, 1 octet). */
#define IOLINK_IDX_DETAILED_DEVICE_STATUS 0x0025U /**< DetailedDeviceStatus (R, O). */
#define IOLINK_IDX_PROCESS_DATA_INPUT 0x0028U     /**< ProcessDataInput: last valid PDin (R, O). */
#define IOLINK_IDX_PROCESS_DATA_OUTPUT 0x0029U /**< ProcessDataOutput: last valid PDout (R, O). */
#define IOLINK_IDX_OFFSET_TIME 0x0030U         /**< OffsetTime (R/W, O, B.2.25). */
#define IOLINK_IDX_ERROR_STATS 0x0040U         /**< Vendor-specific error statistics (0x0040+). */

#define IOLINK_STRING_MAX_LEN 64U        /**< Max StringT length of identification objects. */
#define IOLINK_SERIAL_NUMBER_MAX_LEN 16U /**< Max StringT length of SerialNumber (B.2.13). */
#define IOLINK_PRODUCT_URI_MAX_LEN 100U  /**< Max StringT length of ProductURI (B.2.19). */
/** @} */

/**
 * @defgroup iolinki_protocol_sys_cmd SystemCommand Coding (Index 0x0002, Table B.9)
 * @ingroup iolinki_protocol
 * @brief Values 0x00, 0x07-0x3F and 0x84-0x9F are reserved; 0x40-0x7F are
 *        reserved for profiles and 0xA0-0xFF are vendor specific.
 * @{
 */
#define IOLINK_CMD_PARAM_UPLOAD_START 0x01U       /**< ParamUploadStart (C). */
#define IOLINK_CMD_PARAM_UPLOAD_END 0x02U         /**< ParamUploadEnd (C). */
#define IOLINK_CMD_PARAM_DOWNLOAD_START 0x03U     /**< ParamDownloadStart (C). */
#define IOLINK_CMD_PARAM_DOWNLOAD_END 0x04U       /**< ParamDownloadEnd (C). */
#define IOLINK_CMD_PARAM_DOWNLOAD_STORE 0x05U     /**< ParamDownloadStore (C, 10.4.2). */
#define IOLINK_CMD_PARAM_BREAK 0x06U              /**< ParamBreak: cancel all Param commands (C). */
#define IOLINK_CMD_DEVICE_RESET 0x80U             /**< Device reset (O, 10.7.2). */
#define IOLINK_CMD_APPLICATION_RESET 0x81U        /**< Application reset (H, 10.7.3). */
#define IOLINK_CMD_RESTORE_FACTORY_SETTINGS 0x82U /**< Restore factory settings (O, 10.7.4). */
#define IOLINK_CMD_BACK_TO_BOX 0x83U              /**< Back-to-box (C, 10.7.5). */
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
 * @defgroup iolinki_protocol_isdu_err ISDU Error Codes (0x80xx, Table C.1)
 * @ingroup iolinki_protocol
 * @brief AdditionalCode octet of an ErrorCode 0x80 negative response.
 * @{
 */
#define IOLINK_ISDU_ERROR_NONE 0x00U               /**< No error. */
#define IOLINK_ISDU_ERROR_APP_DEV 0x00U            /**< APP_DEV: application error, no details. */
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL 0x11U  /**< IDX_NOTAVAIL: Index not available. */
#define IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL 0x11U    /**< IDX_NOTAVAIL (C.2.3), explicit name. */
#define IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL 0x12U /**< SUBIDX_NOTAVAIL (C.2.4). */
#define IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL 0x20U  /**< SERV_NOTAVAIL: state-dependent (C.2.5). */
#define IOLINK_ISDU_ERROR_NOT_ACCESSIBLE 0x23U /**< IDX_NOT_ACCESSIBLE: access denied (C.2.8). */
#define IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL \
    0x35U /**< Command function not available (Table C.1). */
#define IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL 0x36U /**< FUNC_UNAVAILTEMP (C.2.15). */
/** @brief Legacy name. 0x30 is PAR_VALOUTOFRNG in Table C.1, not a busy code. */
#define IOLINK_ISDU_ERROR_BUSY 0x30U
#define IOLINK_ISDU_ERROR_VALUE_OUT_OF_RANGE 0x30U /**< PAR_VALOUTOFRNG (C.2.9). */
#define IOLINK_ISDU_ERROR_LENGTH_OVERRUN 0x33U     /**< VAL_LENOVRRUN (C.2.12). */
#define IOLINK_ISDU_ERROR_LENGTH_UNDERRUN 0x34U    /**< VAL_LENUNDRUN (C.2.13). */
#define IOLINK_ISDU_ERROR_WRITE_PROTECTED 0x23U    /**< Access denied, Table C.1. */
#define IOLINK_ISDU_ERROR_PARAM_SET_INVALID 0x40U  /**< PAR_SETINVALID (C.2.16). */
#define IOLINK_ISDU_ERROR_PARAM_INCONSISTENT 0x41U /**< PAR_SETINCONSIST (C.2.17, Table 98). */
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
 * @defgroup iolinki_protocol_device_status Device Status Flags
 * @ingroup iolinki_protocol
 * @{
 */
/* DeviceStatus values, Index 0x0024 (Table B.13). */
#define IOLINK_DEVICE_STATUS_OK 0x00U               /**< Device operating properly. */
#define IOLINK_DEVICE_STATUS_MAINTENANCE 0x01U      /**< Maintenance required. */
#define IOLINK_DEVICE_STATUS_OUT_OF_SPEC 0x02U      /**< Out of specification. */
#define IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK 0x03U /**< Functional check. */
#define IOLINK_DEVICE_STATUS_FAILURE 0x04U          /**< Failure. */
/** @} */

#endif /* IOLINK_PROTOCOL_H */
