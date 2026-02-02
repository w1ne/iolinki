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
 * Device identification structure.
 * Configure these values for your specific device.
 */
typedef struct {
    /* Mandatory ID Indices (0x0010-0x0018) */
    const char *vendor_name;        /* Index 0x0010 */
    const char *vendor_text;        /* Index 0x0011 */
    const char *product_name;       /* Index 0x0012 */
    const char *product_id;         /* Index 0x0013 */
    const char *product_text;       /* Index 0x0014 */
    const char *serial_number;      /* Index 0x0015 */
    const char *hardware_revision;  /* Index 0x0016 */
    const char *firmware_revision;  /* Index 0x0017 */
    const char *application_tag;    /* Index 0x0018 (optional) */
    
    /* Device IDs */
    uint16_t vendor_id;             /* Index 0x0000 */
    uint32_t device_id;             /* Index 0x0000 */
    uint16_t function_id;           /* Index 0x001C */
    
    /* System Info */
    uint8_t min_cycle_time;         /* Index 0x0024 (in 100μs units) */
    uint16_t revision_id;           /* Index 0x001E */
} iolink_device_info_t;

/**
 * @brief Initialize device information
 * @param info Pointer to device info structure
 */
void iolink_device_info_init(const iolink_device_info_t *info);

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
int iolink_device_info_set_application_tag(const char *tag, uint8_t len);

#endif // IOLINK_DEVICE_INFO_H
