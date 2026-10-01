/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWITCHING_SENSOR_H
#define SWITCHING_SENSOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct
{
    uint16_t threshold;
    uint16_t hysteresis;
    uint16_t sample;
    bool inverted;
    bool active;
    bool valid;
    uint8_t pd[3];
} switching_sensor_t;
void switching_sensor_init(switching_sensor_t* sensor);
bool switching_sensor_sample(switching_sensor_t* sensor, uint16_t hundredths, bool valid);
uint8_t switching_sensor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                                 const uint8_t* input, size_t length, uint8_t* output,
                                 size_t* output_length);
#endif
