#ifndef IOLINK_APPLICATION_H
#define IOLINK_APPLICATION_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file application.h
 * @brief IO-Link Application Layer API for Process Data
 */

/**
 * @brief Update Process Data Input (Device -> Master)
 * 
 * @param data Pointer to input data
 * @param len Length in bytes
 * @return int 0 on success, negative on error
 */
int iolink_pd_input_update(const uint8_t *data, size_t len);

/**
 * @brief Read Process Data Output (Master -> Device)
 * 
 * @param data Pointer to buffer to store output data
 * @param len Max length to read
 * @return int Number of bytes read, negative on error
 */
int iolink_pd_output_read(uint8_t *data, size_t len);

#endif // IOLINK_APPLICATION_H
