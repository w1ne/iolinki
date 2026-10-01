/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REFERENCE_DEVICE_H
#define REFERENCE_DEVICE_H
#include "iolinki/device.h"

/* Config and identity must outlive the stack. Keep the entire app in static
 * storage on embedded targets; each target supplies a real PHY and clock. */
typedef struct
{
    iolink_device_ctx_t device;
    iolink_device_config_t config;
    uint16_t counter;
    uint8_t input[3];
    bool button;
    bool led;
} reference_device_t;

int reference_device_init(reference_device_t* app, const iolink_phy_api_t* phy);
/* Call once per application sampling tick, independently of the much faster
 * protocol processing loop. Input: big-endian counter + button; output: LED bit 0. */
int reference_device_tick(reference_device_t* app, bool button);
#endif
