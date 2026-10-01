/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switching_sensor.h"
#include <assert.h>

int main(void)
{
    switching_sensor_t sensor;
    switching_sensor_init(&sensor);
    assert(!switching_sensor_sample(&sensor, 4900, true));
    assert(switching_sensor_sample(&sensor, 5000, true));
    assert(switching_sensor_sample(&sensor, 4900, true));
    assert(!switching_sensor_sample(&sensor, 4800, true));
    uint8_t input[] = {0, 1}, output[8];
    size_t length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0102, 0, true, input + 1, 1, output, &length) == 0);
    assert(switching_sensor_sample(&sensor, 4700, true));
    assert(!switching_sensor_sample(&sensor, 6000, false));
    assert(sensor.pd[2] == 0); /* Invalid reading clears output and validity. */
    switching_sensor_sample(&sensor, 6000, true);
    input[0] = 1;
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0103, 0, true, input, 1, output, &length) == 0);
    assert(sensor.threshold == 6000);
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0100, 0, false, NULL, 0, output, &length) == 0);
    assert(length == 2 && output[0] == 0x17 && output[1] == 0x70);
    uint16_t original = sensor.threshold;
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0100, 0, true, input, 1, output, &length) != 0);
    assert(sensor.threshold == original);
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0100, 1, false, NULL, 0, output, &length) != 0);
    length = 1;
    assert(switching_sensor_service(&sensor, 0x0100, 0, false, NULL, 0, output, &length) != 0);
    input[0] = 0xFF;
    input[1] = 0xFF;
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0101, 0, true, input, 2, output, &length) != 0);
    assert(sensor.hysteresis == 200);
    switching_sensor_sample(&sensor, 0, false);
    input[0] = 1;
    length = sizeof(output);
    assert(switching_sensor_service(&sensor, 0x0103, 0, true, input, 1, output, &length) != 0);
    assert(sensor.threshold == original);
    return 0;
}
