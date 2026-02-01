#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stdint.h>
#include "iolinki/dll.h"

/**
 * @file dll_internal.h
 * @brief Internal DLL definitions and structures
 */

#define IOLINK_M_SEQ_TYPE0_LEN 2

typedef struct {
    uint8_t master_command;
    uint8_t ck;
} iolink_m_seq_type0_t;

#endif // IOLINK_DLL_INTERNAL_H
