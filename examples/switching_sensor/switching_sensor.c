/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switching_sensor.h"
#include "iolinki/protocol.h"
#include <string.h>

void switching_sensor_init(switching_sensor_t* sensor)
{
    memset(sensor, 0, sizeof(*sensor));
    sensor->threshold = 5000;
    sensor->hysteresis = 200;
}

bool switching_sensor_sample(switching_sensor_t* sensor, uint16_t hundredths, bool valid)
{
    sensor->sample = hundredths;
    sensor->valid = valid;
    if (!valid) {
        sensor->active = false;
    }
    else if (hundredths >= sensor->threshold) {
        sensor->active = true;
    }
    else if (hundredths <= sensor->threshold - sensor->hysteresis) {
        sensor->active = false;
    }
    bool output = valid && (sensor->active != sensor->inverted);
    sensor->pd[0] = (uint8_t) (hundredths >> 8);
    sensor->pd[1] = (uint8_t) hundredths;
    sensor->pd[2] = valid ? (uint8_t) (2U | (output ? 1U : 0U)) : 0U;
    return output;
}

uint8_t switching_sensor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                                 const uint8_t* input, size_t length, uint8_t* output,
                                 size_t* output_length)
{
    switching_sensor_t* sensor = user;
    if (subindex != 0U) {
        return IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
    }
    if (index < 0x0100U || index > 0x0103U) {
        return 0x11U; /* Index not available. */
    }
    if (write) {
        if (index <= 0x0101U) {
            if (length != 2U) return 0x33U; /* Parameter length invalid. */
            uint16_t value = (uint16_t) ((uint16_t) input[0] << 8) | input[1];
            if ((index == 0x0100U && value < sensor->hysteresis) ||
                (index == 0x0101U && value > sensor->threshold))
                return 0x30U;
            if (index == 0x0100U)
                sensor->threshold = value;
            else
                sensor->hysteresis = value;
        }
        else if (index == 0x0102U) {
            if (length != 1U) return 0x33U;
            if (input[0] > 1U) return 0x30U;
            sensor->inverted = input[0] != 0U;
        }
        else {
            if (length != 1U) return 0x33U;
            if (input[0] != 1U || !sensor->valid || sensor->sample < sensor->hysteresis)
                return 0x30U;
            sensor->threshold = sensor->sample;
        }
        *output_length = 0;
    }
    else {
        if (index == 0x0103U) return IOLINK_ISDU_ERROR_NOT_ACCESSIBLE;
        size_t needed = index == 0x0102U ? 1U : 2U;
        if (*output_length < needed) return 0x33U;
        uint16_t value = index == 0x0100U ? sensor->threshold : sensor->hysteresis;
        output[0] = needed == 1U ? (uint8_t) sensor->inverted : (uint8_t) (value >> 8);
        if (needed == 2U) output[1] = (uint8_t) value;
        *output_length = needed;
    }
    return 0;
}
