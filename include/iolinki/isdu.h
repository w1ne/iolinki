#ifndef IOLINK_ISDU_H
#define IOLINK_ISDU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iolinki/config.h"

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
    ISDU_STATE_HEADER_INITIAL,   /* First byte of a new Request (Read/Write + Length) */
    ISDU_STATE_HEADER_EXT_LEN,   /* Extended length byte (if Initial Length == 0) */
    ISDU_STATE_HEADER_INDEX_HIGH,
    ISDU_STATE_HEADER_INDEX_LOW,
    ISDU_STATE_HEADER_SUBINDEX,
    ISDU_STATE_DATA_COLLECT,     /* Collecting data bytes for WRITE */
    ISDU_STATE_SEGMENT_COLLECT,  /* Collecting next segment for multi-frame WRITE */
    ISDU_STATE_SERVICE_EXECUTE,
    ISDU_STATE_RESPONSE_READY,
    ISDU_STATE_BUSY              /* Internal processing, Master should retry */
} isdu_state_t;

typedef struct {
    isdu_state_t state;
    uint8_t buffer[IOLINK_ISDU_BUFFER_SIZE];
    size_t buffer_idx;
    iolink_isdu_header_t header;
    uint8_t response_buf[IOLINK_ISDU_BUFFER_SIZE];
    size_t response_idx;
    size_t response_len;
    
    /* Segmentation and Flow Control */
    isdu_state_t next_state;     /* State to resume after SEGMENT_COLLECT */
    uint8_t segment_seq;         /* Expected sequence number for next segment */
    bool is_segmented;           /* True if current request/response is segmented */
    bool is_response_control_sent; /* True if Control Byte was sent for current segment */
    uint8_t error_code;          /* 0 if OK, otherwise IO-Link ISDU error code */
    
    /* Pointers to external dependencies */
    void *event_ctx;
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
