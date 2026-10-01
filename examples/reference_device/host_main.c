/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "reference_device.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include <stdio.h>
#include <string.h>

/* In-memory test transport: no UART, transceiver, or physical IO-Link master. */
typedef struct
{
    uint8_t rx[64], tx[64];
    size_t rx_len, rx_pos, tx_len;
    bool wakeup;
} link_t;

static int receive(void* user, uint8_t* byte)
{
    link_t* link = user;
    if (link->rx_pos == link->rx_len) return 0;
    *byte = link->rx[link->rx_pos++];
    return 1;
}

static int send_bytes(void* user, const uint8_t* data, size_t len)
{
    link_t* link = user;
    if (len > sizeof(link->tx)) return -1;
    memcpy(link->tx, data, len);
    link->tx_len = len;
    return (int) len;
}

static int wakeup(void* user)
{
    link_t* link = user;
    int result = link->wakeup ? 1 : 0;
    link->wakeup = false;
    return result;
}

static void exchange(reference_device_t* app, link_t* link, const uint8_t* frame, size_t len)
{
    memcpy(link->rx, frame, len);
    link->rx_len = len;
    link->rx_pos = 0;
    link->tx_len = 0;
    iolink_device_process(&app->device);
}

int main(void)
{
    reference_device_t app;
    link_t link = {0};
    const iolink_phy_api_t phy = {
        .user = &link,
        .send = send_bytes,
        .recv_byte = receive,
        .detect_wakeup = wakeup,
    };
    if (reference_device_init(&app, &phy) != 0) return 1;
    iolink_device_set_timing_enforcement(&app.device, false);
    link.wakeup = true;
    iolink_device_process(&app.device);
    uint8_t transition[2] = {IOLINK_MC_TRANSITION_COMMAND, 0};
    transition[1] = iolink_checksum6(transition, sizeof(transition));
    exchange(&app, &link, transition, sizeof(transition));

    for (unsigned cycle = 0; cycle < 100; cycle++) {
        /* Type 2_V: MC, CKT, 1 PD-out, 2 OD. Button remains released here. */
        uint8_t frame[5] = {0x80, IOLINK_MSEQ_TYPE_2, (uint8_t) (cycle & 1U), 0, 0};
        frame[1] |= iolink_checksum6(frame, sizeof(frame));
        if (reference_device_tick(&app, false) != 0) return 2;
        exchange(&app, &link, frame, sizeof(frame));
        if (iolink_device_get_state(&app.device) != IOLINK_DLL_STATE_OPERATE) return 3;
        /* Device response: 3 input bytes, 2 OD bytes, CKS. */
        if ((link.tx_len != 6) || (memcmp(link.tx, app.input, sizeof(app.input)) != 0)) return 4;
        uint8_t checksum = link.tx[5] & 0x3FU;
        link.tx[5] &= 0xC0U;
        if (checksum != iolink_checksum6(link.tx, link.tx_len)) return 5;
        uint8_t output = 0;
        if ((iolink_device_pd_output_read(&app.device, &output, 1) != 1) ||
            (output != (cycle & 1U)))
            return 6;
    }
    /* Parameter API example: application tag, index 0x18. This call is local;
     * wire-level ISDU exchange is covered by the stack's separate tests. */
    const uint8_t tag[] = "bench-example";
    uint8_t readback[32];
    if (iolink_params_ctx_set(&app.device.params, 0x18, 0, tag, sizeof(tag) - 1, false) != 0)
        return 7;
    int len = iolink_params_ctx_get(&app.device.params, 0x18, 0, readback, sizeof(readback));
    if ((len != (int) sizeof(tag) - 1) || (memcmp(tag, readback, (size_t) len) != 0)) return 8;
    if (reference_device_tick(&app, true) != 0) return 9;
    if (!iolink_events_pending(iolink_device_get_events_ctx(&app.device))) return 10;
    printf(
        "SIMULATION PASS: 100 cyclic frame exchanges, counter, LED output, checksums, local "
        "parameter readback and queued button event\n");
    printf(
        "No physical hardware tested; event acknowledgement and persistent storage are not "
        "demonstrated here.\n");
    return 0;
}
