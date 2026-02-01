#ifndef IOLINK_ISDU_H
#define IOLINK_ISDU_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file isdu.h
 * @brief IO-Link Indexed Service Data Unit (ISDU) Acyclic Messaging
 */

typedef enum {
    IOLINK_ISDU_SERVICE_READ = 0,
    IOLINK_ISDU_SERVICE_WRITE = 1
} iolink_isdu_service_type_t;

typedef struct {
    uint16_t index;
    uint8_t subindex;
    iolink_isdu_service_type_t type;
    size_t length;
} iolink_isdu_header_t;

/**
 * @brief Initialize ISDU engine
 */
void iolink_isdu_init(void);

/**
 * @brief Collect a byte from an M-sequence (on-request data)
 * @param byte Data byte from M-sequence
 * @return 0 if still collecting, 1 if ISDU request complete, negative on error
 */
int iolink_isdu_collect_byte(uint8_t byte);

/**
 * @brief Get the response byte for the current ISDU service
 * @param byte Pointer to store the byte
 * @return 1 if byte provided, 0 if nothing to send, negative on error
 */
int iolink_isdu_get_response_byte(uint8_t *byte);

#endif // IOLINK_ISDU_H
