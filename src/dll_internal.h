#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stdint.h>
#include "iolinki/dll.h"

/**
 * @file dll_internal.h
 * @brief Internal DLL definitions and structures
 */

#define IOLINK_M_SEQ_TYPE0_LEN 2
#define IOLINK_M_SEQ_TYPE1_1_LEN(pd_len) (1 + (pd_len) + 1) /* MC + PD + OD */
#define IOLINK_M_SEQ_TYPE1_2_LEN(pd_len) (1 + (pd_len) + 1) /* MC + PD + OD */
#define IOLINK_M_SEQ_TYPE2_1_LEN(pd_len) (1 + (pd_len) + 1) /* MC + PD + OD (OD is high/low) */ /* Wait, Type 2 OD is part of checksum handling usually? No. */
/* 
 * Type 1: MC(8) + CKT(8) + PD(var) + OD(8) + CK(8) -- wait, no. 
 * V1.1 Spec:
 * Type 0: MC(8) + CKT(8) ... NO. 
 * Type 0: 2 bytes: MC(8) + CKT/CK(8). Period.
 * 
 * Type 1_x:
 * PC -> Dev: MC(8) + CKT(8) + PD(var) + OD(8) + CK(8)  <-- This structure varies.
 * 
 * Let's simplify. We receive bytes. We need to know how many bytes to expect based on MC.
 * Frame structure is determined by Preoperate (Type 0) vs Operate (Configured Type).
 */

typedef struct {
    uint8_t master_command;
    uint8_t ck;
} iolink_m_seq_type0_t;

/* Definitions for OD handling */
typedef struct {
    uint8_t od_data;
    uint8_t valid;
} iolink_od_req_t;

#endif // IOLINK_DLL_INTERNAL_H
