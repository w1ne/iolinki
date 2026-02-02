#ifndef IOLINK_DATA_STORAGE_H
#define IOLINK_DATA_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief IO-Link Data Storage (DS) for parameter backup and restore
 */

/**
 * @brief DS engine states
 */
typedef enum {
    IOLINK_DS_STATE_IDLE,
    IOLINK_DS_STATE_UPLOAD_REQ,
    IOLINK_DS_STATE_UPLOADING,
    IOLINK_DS_STATE_DOWNLOAD_REQ,
    IOLINK_DS_STATE_DOWNLOADING,
    IOLINK_DS_STATE_LOCKED
} iolink_ds_state_t;

/**
 * @brief Storage Abstraction Layer for DS persistence
 */
typedef struct {
    int (*read)(uint32_t addr, uint8_t *buf, size_t len);      /**< Read data from storage */
    int (*write)(uint32_t addr, const uint8_t *buf, size_t len); /**< Write data to storage */
    int (*erase)(uint32_t addr, size_t len);                   /**< Erase storage (optional) */
} iolink_ds_storage_api_t;

/**
 * @brief Data Storage context
 */
typedef struct {
    iolink_ds_state_t state;
    const iolink_ds_storage_api_t *storage;
    uint16_t current_checksum;
    uint16_t master_checksum;
} iolink_ds_ctx_t;

/**
 * @brief Initialize Data Storage engine
 * @param ctx DS context
 * @param storage Storage implementation hooks (can be NULL for volatile storage)
 */
void iolink_ds_init(iolink_ds_ctx_t *ctx, const iolink_ds_storage_api_t *storage);

/**
 * @brief Calculate checksum for a parameter set
 * @param data Pointer to parameter data
 * @param len Length in bytes
 * @return uint16_t Calculated checksum
 */
uint16_t iolink_ds_calc_checksum(const uint8_t *data, size_t len);

/**
 * @brief Process DS state machine
 * @param ctx DS context
 */
void iolink_ds_process(iolink_ds_ctx_t *ctx);

/**
 * @brief Trigger DS Check (Master comparison)
 * @param ctx DS context
 * @param master_checksum Checksum provided by Master
 */
void iolink_ds_check(iolink_ds_ctx_t *ctx, uint16_t master_checksum);

#endif // IOLINK_DATA_STORAGE_H
