/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switching_sensor.h"
#include "iolinki/device.h"
#include "iolinki/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int send_bytes(void* user, const uint8_t* data, size_t length)
{
    (void) user;
    (void) data;
    return (int) length;
}
static int receive_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}
int main(void)
{
    switching_sensor_t sensor;
    switching_sensor_init(&sensor);
    iolink_device_config_t config = {0};
    config.phy.send = send_bytes;
    config.phy.recv_byte = receive_byte;
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config.stack.pd_in_len = 3;
    config.stack.pd_out_len = 0;
    config.stack.min_cycle_time = 10;
    config.user = &sensor;
    config.vendor_service = switching_sensor_service;
    static const iolink_device_info_t identity = {.vendor_id = 1234,
                                                  .device_id = 5679,
                                                  .revision_id = 0x11,
                                                  .vendor_name = "iolinki example",
                                                  .product_name = "Switching sensor",
                                                  .product_id = "switching-sensor",
                                                  .min_cycle_time = 10};
    config.device_info = &identity;
    iolink_device_ctx_t device;
    assert(iolink_device_init(&device, &config) == 0);
    bool previous = false;
    const uint16_t samples[] = {4900, 5100, 4900, 4800};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        bool active = switching_sensor_sample(&sensor, samples[i], true);
        assert(iolink_device_pd_input_update(&device, sensor.pd, 3, true) == 0);
        if (active && !previous)
            iolink_event_trigger(iolink_device_get_events_ctx(&device), 0x8CA0,
                                 IOLINK_EVENT_TYPE_NOTIFICATION);
        previous = active;
    }
    assert(iolink_events_pending(iolink_device_get_events_ctx(&device)));
    /* Write teach command through the real ISDU OD transport, then read SP1. */
    const uint8_t teach[] = {0x36, 0x01, 0x03, 0x00, 0x01, 0x35};
    for (size_t i = 0; i < sizeof(teach); i++)
        iolink_isdu_od_write(&device.dll.isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                             teach + i, 1);
    iolink_isdu_process(&device.dll.isdu);
    assert(sensor.threshold == 4800);
    assert(device.dll.isdu.response_len == 0);
    /* Restart a service by IDLE before the next START. */
    uint8_t ignored;
    iolink_isdu_od_read(&device.dll.isdu, IOLINK_FLOWCTRL_IDLE, &ignored, 1);
    const uint8_t read[] = {0xB5, 0x01, 0x00, 0x00, 0xB4};
    for (size_t i = 0; i < sizeof(read); i++)
        iolink_isdu_od_write(&device.dll.isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                             read + i, 1);
    iolink_isdu_process(&device.dll.isdu);
    assert(device.dll.isdu.response_len == 2);
    assert(device.dll.isdu.response_buf[0] == 0x12 && device.dll.isdu.response_buf[1] == 0xC0);
    puts("SIMULATION PASS: hysteresis, PD publication, event and wire ISDU teach/read");
    return 0;
}
