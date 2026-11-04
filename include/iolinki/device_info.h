/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DEVICE_INFO_H
#define IOLINK_DEVICE_INFO_H

#include <stdint.h>

/**
 * @file device_info.h
 * @brief Device identification and mandatory ISDU indices
 *
 * Implements mandatory indices required by IO-Link V1.1.5 specification.
 */

/**
 * @defgroup iolinki_device_info Device Identification
 * @brief Device identification data and the mandatory identity ISDU indices.
 * @{
 */

/**
 * @brief Device identification structure.
 *
 * Configure these values for your specific device. Each field maps to a
 * mandatory or optional IO-Link identity ISDU index.
 */
typedef struct
{
    /* Mandatory ID Indices (0x0010-0x0018) */
    const char* vendor_name;       /**< Vendor name string (Index 0x0010). */
    const char* vendor_text;       /**< Vendor text string (Index 0x0011). */
    const char* product_name;      /**< Product name string (Index 0x0012). */
    const char* product_id;        /**< Product ID string (Index 0x0013). */
    const char* product_text;      /**< Product text string (Index 0x0014). */
    const char* product_uri;       /**< Product URI string (Index 0x001B, optional). */
    const char* serial_number;     /**< Serial number string (Index 0x0015). */
    const char* hardware_revision; /**< Hardware revision string (Index 0x0016). */
    const char* firmware_revision; /**< Firmware revision string (Index 0x0017). */
    const char* application_tag;   /**< Application tag string (Index 0x0018, optional). */

    /* Device IDs (Mandatory Indices) */
    uint16_t vendor_id;              /**< Vendor ID (Direct Parameter page 1, 0x07-0x08). */
    uint32_t device_id;              /**< Device ID (Direct Parameter page 1, 0x09-0x0B). */
    uint16_t function_id;            /**< Function ID (Direct Parameter page 1, 0x0C-0x0D). */
    uint16_t profile_characteristic; /**< Profile characteristic (Index 0x000D). */

    /* System Info */
    uint8_t min_cycle_time;          /**< MinCycleTime in 0.1 ms units (page 1, 0x02); an octet
                                          caps it at 25.5 ms. */
    uint16_t revision_id;            /**< RevisionID (Direct Parameter page 1, 0x04). */
    uint8_t device_status;           /**< DeviceStatus (Index 0x0024). */
    uint16_t detailed_device_status; /**< DetailedDeviceStatus (Index 0x0025, optional). */

    /* Access Control */
    uint16_t access_locks; /**< Device access locks bitmask (Index 0x000C). */
} iolink_device_info_t;

/**
 * @brief Per-device identification runtime context.
 *
 * Wraps the static configured identity with mutable runtime values
 * (application tag, access locks) and factory defaults.
 */
typedef struct
{
    const iolink_device_info_t* configured; /**< Static configured identity. */
    iolink_device_info_t defaults;          /**< Factory-default identity snapshot. */
    char application_tag[33];               /**< Runtime application tag (32 bytes + NUL). */
    uint16_t access_locks;                  /**< Runtime device access locks value. */
    uint16_t default_access_locks;          /**< State-of-delivery access locks (Table 101). */
} iolink_device_info_ctx_t;

/**
 * @brief Initialize a device-info context.
 * @param ctx         Context to initialize.
 * @param configured  Static device identity to bind (must remain valid).
 */
void iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
                                 const iolink_device_info_t* configured);

/**
 * @brief Get the effective device identity from a context.
 * @param ctx Device-info context.
 * @return Pointer to the effective device identity.
 */
const iolink_device_info_t* iolink_device_info_ctx_get(const iolink_device_info_ctx_t* ctx);

/**
 * @brief Set the runtime application tag (Index 0x0018) on a context.
 * @param ctx  Device-info context.
 * @param tag  String data (max 32 bytes).
 * @param len  Length of @p tag in bytes.
 * @return 0 on success, negative on error.
 */
int iolink_device_info_ctx_set_application_tag(iolink_device_info_ctx_t* ctx, const char* tag,
                                               uint8_t len);

/**
 * @brief Get the device access locks (Index 0x000C) from a context.
 * @param ctx Device-info context.
 * @return 16-bit access locks value.
 */
uint16_t iolink_device_info_ctx_get_access_locks(const iolink_device_info_ctx_t* ctx);

/**
 * @brief Set the device access locks (Index 0x000C) on a context.
 * @param ctx    Device-info context.
 * @param locks  16-bit access locks value.
 */
void iolink_device_info_ctx_set_access_locks(iolink_device_info_ctx_t* ctx, uint16_t locks);

/**
 * @brief Get the state-of-delivery access locks of a context (Table 101).
 * @param ctx Device-info context.
 * @return Access locks value configured at initialization.
 */
uint16_t iolink_device_info_ctx_get_default_access_locks(const iolink_device_info_ctx_t* ctx);

/**
 * @brief Restore the access locks to the state of delivery (Table 101).
 * @param ctx Device-info context.
 */
void iolink_device_info_ctx_reset_access_locks(iolink_device_info_ctx_t* ctx);

/**
 * @brief Initialize device information
 * @param info Pointer to device info structure
 */
void iolink_device_info_init(const iolink_device_info_t* info);

/**
 * @brief Get device information
 * @return Pointer to device info structure
 */
const iolink_device_info_t* iolink_device_info_get(void);

/**
 * @brief Set the Application Tag (Index 0x18)
 * @param tag String data (max 32 bytes)
 * @param len Length of string
 * @return 0 on success, negative on error
 */
int iolink_device_info_set_application_tag(const char* tag, uint8_t len);

/**
 * @brief Get Device Access Locks (Index 0x000C)
 * @return 16-bit access locks value
 */
uint16_t iolink_device_info_get_access_locks(void);

/**
 * @brief Set Device Access Locks (Index 0x000C)
 * @param locks 16-bit access locks value
 */
void iolink_device_info_set_access_locks(uint16_t locks);

/**
 * @brief Restore the legacy Device Access Locks to the state of delivery.
 */
void iolink_device_info_reset_access_locks(void);

/**
 * @brief Get the legacy state-of-delivery access locks (Table 101).
 * @return Access locks value configured at initialization.
 */
uint16_t iolink_device_info_get_default_access_locks(void);

/** @} */ /* end of iolinki_device_info */

#endif  // IOLINK_DEVICE_INFO_H
