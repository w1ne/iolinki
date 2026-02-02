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
    uint8_t type; /* Read/Write */
    uint8_t length;
    uint16_t index;
    uint8_t subindex;
} iolink_isdu_header_t;

typedef enum {
    ISDU_STATE_IDLE,
    ISDU_STATE_HEADER_INDEX_HIGH,
    ISDU_STATE_HEADER_INDEX_LOW,
    ISDU_STATE_HEADER_SUBINDEX,
    ISDU_STATE_DATA_COLLECT,
    ISDU_STATE_SERVICE_EXECUTE,
    ISDU_STATE_RESPONSE_READY
} isdu_state_t;

typedef struct {
    isdu_state_t state;
    uint8_t buffer[256];
    size_t buffer_idx;
    iolink_isdu_header_t header;
    uint8_t response_buf[256];
    size_t response_idx;
    size_t response_len;
    
    /* Pointers to external dependencies if needed */
    void *event_ctx; /* (void*) to avoid circular dependency, usually iolink_events_ctx_t* */
} iolink_isdu_ctx_t;

/**
 * @brief Initialize ISDU engine
 * @param ctx ISDU context
 */
void iolink_isdu_init(iolink_isdu_ctx_t *ctx);

/**
 * @brief Process ISDU engine (execute services, prepare responses)
 * @param ctx ISDU context
 */
void iolink_isdu_process(iolink_isdu_ctx_t *ctx);

/**
 * @brief Collect a byte from an M-sequence (on-request data)
 * @param ctx ISDU context
 * @param byte Data byte from M-sequence
 * @return 0 if still collecting, 1 if ISDU request complete, negative on error
 */
int iolink_isdu_collect_byte(iolink_isdu_ctx_t *ctx, uint8_t byte);

/**
 * @brief Get next response byte to send
 * @param ctx ISDU context
 * @param byte Pointer to store the byte
 * @return 1 if byte available from ISDU response, 0 if idle/no data
 */
int iolink_isdu_get_response_byte(iolink_isdu_ctx_t *ctx, uint8_t *byte);

#endif // IOLINK_ISDU_H
