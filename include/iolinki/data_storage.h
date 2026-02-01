#ifndef IOLINK_DATA_STORAGE_H
#define IOLINK_DATA_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief IO-Link Data Storage (DS) for parameter backup and restore
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
    int (*read)(uint32_t addr, uint8_t *buf, size_t len);
    int (*write)(uint32_t addr, const uint8_t *buf, size_t len);
    int (*erase)(uint32_t addr, size_t len);
} iolink_ds_storage_api_t;

/**
 * @brief Initialize Data Storage engine
 * @param storage Storage implementation hooks
 */
void iolink_ds_init(const iolink_ds_storage_api_t *storage);

/**
 * @brief Calculate checksum for a parameter set
 * @param data Pointer to parameter data
 * @param len Length in bytes
 * @return uint16_t Calculated checksum
 */
uint16_t iolink_ds_calc_checksum(const uint8_t *data, size_t len);

/**
 * @brief Process DS state machine
 */
void iolink_ds_process(void);

/**
 * @brief Trigger DS Check (Master comparison)
 * @param master_checksum Checksum provided by Master
 */
void iolink_ds_check(uint16_t master_checksum);

#endif // IOLINK_DATA_STORAGE_H
