/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Protocol-phase/cycle observation regression. Synthetic timestamps are not pin timing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "iolinki/device.h"
#include "iolinki/crc.h"
#include "iolinki/time_utils.h"

static uint64_t now_us;
uint64_t iolink_time_get_us(void)
{
    return now_us;
}
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now_us / 1000U);
}

typedef struct
{
    uint8_t request[4];
    size_t count, pos, reply_len;
    uint64_t first_byte_us;
} link_t;

static int receive(void* user, uint8_t* byte)
{
    link_t* link = user;
    if (link->pos >= link->count) return 0;
    now_us = link->first_byte_us + link->pos * 48U;
    *byte = link->request[link->pos++];
    return 1;
}
static int send_reply(void* user, const uint8_t* data, size_t length)
{
    link_t* link = user;
    (void) data;
    link->reply_len = length;
    now_us += length * 48U; /* Distinct completion duration, independent of cycle start. */
    return (int) length;
}
static void exchange(iolink_device_ctx_t* device, link_t* link, const uint8_t* request,
                     size_t count, uint64_t first_byte_us)
{
    memcpy(link->request, request, count);
    link->count = count;
    link->pos = 0U;
    link->reply_len = 0U;
    link->first_byte_us = first_byte_us;
    now_us = first_byte_us;
    iolink_device_process(device);
}
static void scenario(uint64_t base)
{
    link_t link = {0};
    iolink_device_config_t config = {0};
    iolink_device_ctx_t device;
    config.phy.user = &link;
    config.phy.recv_byte = receive;
    config.phy.send = send_reply;
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config.stack.pd_in_len = 32U;
    config.stack.pd_out_len = 1U;
    config.stack.od_len = 1U;
    config.stack.min_cycle_time = 30U;
    now_us = base;
    assert(iolink_device_init(&device, &config) == 0);
    iolink_device_set_timing_enforcement(&device, true);
    iolink_device_set_t_ren_limit_us(&device, 20000U);
    assert(iolink_dll_set_baudrate(&device.dll, IOLINK_BAUDRATE_COM3) == 0);
    uint8_t min_cycle[2] = {0xA2U, 0U};
    min_cycle[1] = iolink_checksum6(min_cycle, 2U);
    exchange(&device, &link, min_cycle, 2U, base);
    assert(device.dll.state == IOLINK_DLL_STATE_PREOPERATE);
    uint8_t capability[2] = {0xA3U, 0U};
    capability[1] = iolink_checksum6(capability, 2U);
    exchange(&device, &link, capability, 2U, base + 435U);
    uint8_t operate[3] = {0x20U, 0U, 0x99U};
    operate[1] = iolink_checksum6(operate, 3U);
    exchange(&device, &link, operate, 3U, base + 870U);
    assert(device.dll.state == IOLINK_DLL_STATE_ESTAB_COM);
    assert(device.dll.t_cycle_violations == 0U);
    uint8_t write[4] = {0x00U, 0x80U, 7U, 0U};
    write[1] |= iolink_checksum6(write, 4U);
    uint8_t read[3] = {0x80U, 0x80U, 9U};
    read[1] |= iolink_checksum6(read, 3U);
    const uint64_t start = base + 1305U;
    exchange(&device, &link, write, 4U, start);
    assert(device.dll.state == IOLINK_DLL_STATE_OPERATE);
    assert(link.reply_len == 33U);
    exchange(&device, &link, read, 3U, start + 3000U);
    assert(link.reply_len == 34U);
    assert(device.dll.response_time_us == 34U * 48U);
    exchange(&device, &link, write, 4U, start + 6000U);
    assert(device.dll.response_time_us == 33U * 48U);
    assert(device.dll.t_cycle_violations == 0U);
    exchange(&device, &link, read, 3U, start + 8999U); /* genuinely 1us too early */
    assert(device.dll.t_cycle_violations == 1U);
    read[1] ^= 1U;
    exchange(&device, &link, read, 3U, start + 9999U);
    assert(link.reply_len == 0U && device.dll.crc_errors == 1U);
    assert(device.dll.t_cycle_violations == 1U); /* Invalid frame cannot reset the baseline. */
    read[1] ^= 1U;
    exchange(&device, &link, read, 3U, start + 11999U);
    assert(device.dll.t_cycle_violations == 1U);
    assert(device.dll.t_byte_violations == 0U && device.dll.t_ren_violations == 0U);
}
int main(void)
{
    scenario(1000000U);
    scenario((uint64_t) UINT32_MAX - 1000U); /* cross the 32-bit microsecond boundary */
    puts("STARTUP/PREOPERATE recovery, OPERATE start-to-start, genuine early cycle, "
         "CRC and completion reference passed");
    return 0;
}
