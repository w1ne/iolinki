/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real UART/TIOL112 evaluation profile. No forced OPERATE and no fake PHY.
 * Experimental identity only: replace identity/IODD before any product use.
 */
#include "board.h"
#include "iolinki/device.h"
#include "iolinki/protocol.h"
#include <string.h>

#ifndef IOLINK_RIG_COM
#define IOLINK_RIG_COM 3
#endif
#if IOLINK_RIG_COM == 3
#define RIG_BAUD IOLINK_BAUDRATE_COM3
#define RIG_MIN_CYCLE 30U
#elif IOLINK_RIG_COM == 2
#define RIG_BAUD IOLINK_BAUDRATE_COM2
#define RIG_MIN_CYCLE 150U
#else
#error "The evaluation profile supports IOLINK_RIG_COM=2 or 3"
#endif

/* Exported debugger evidence; counters are not used as timing oracles. */
iolink_device_ctx_t protocol_device;
volatile uint32_t protocol_ready;
volatile int protocol_init_error;
volatile uint32_t rig_requests;
volatile uint32_t rig_last_selector;
volatile uint32_t rig_service_calls;
volatile int rig_phy_fault;
volatile uint32_t rig_generation;
static iolink_device_config_t config;
static iolink_tiol112_ctx_t tiol112;
static uint8_t blocks[256][32];
static uint8_t image[1024];

static const iolink_device_info_t identity = {
    .vendor_name = "iolinki example",
    .vendor_text = "Experimental identity; replace before shipping",
    .product_name = "Same-cycle protocol evaluation",
    .product_id = "type2v-32-1-od1",
    .product_text = "Prebuilt block selector and read-only 1KiB image",
    .serial_number = "RIG-001",
    .hardware_revision = "G0B1-TIOL112-evaluation",
    .firmware_revision = "1",
    .vendor_id = 1234,
    .device_id = 5678,
    .min_cycle_time = RIG_MIN_CYCLE, /* API units 100us; DPP encoding rounds up. */
    .revision_id = 0x11,
};

static uint8_t payload(size_t offset)
{
    return (uint8_t) ((offset * 13U) ^ (offset >> 3U) ^ 0x5aU);
}

static const uint8_t* select_block(void* user, const uint8_t* pdout, uint8_t out_len,
                                   uint8_t in_len, bool* valid)
{
    (void) user;
    if ((pdout == NULL) || (out_len != 1U) || (in_len != 32U)) {
        *valid = false;
        return NULL;
    }
    ++rig_requests;
    rig_last_selector = pdout[0];
    *valid = true;
    return blocks[pdout[0]];
}

static uint8_t image_service(void* user, uint16_t index, uint8_t subindex, bool write,
                              const uint8_t* input, size_t length, uint8_t* output,
                              size_t* output_length)
{
    (void) user;
    (void) input;
    (void) length;
    ++rig_service_calls;
    if ((index < 0x0100U) || (index > 0x0104U)) {
        return IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
    }
    if (subindex != 0U) return IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
    if (write) return IOLINK_ISDU_ERROR_WRITE_PROTECTED;
    const size_t offset = (size_t) (index - 0x0100U) * 232U;
    const size_t count = (index == 0x0104U) ? 96U : 232U;
    if ((output == NULL) || (output_length == NULL) || (*output_length < count)) {
        return IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
    }
    memcpy(output, &image[offset], count);
    *output_length = count;
    return 0;
}

int main(void)
{
    board_init();
    for (unsigned selector = 0U; selector < 256U; ++selector) {
        blocks[selector][0] = (uint8_t) selector;
        for (unsigned i = 1U; i < 32U; ++i) {
            blocks[selector][i] = payload((size_t) selector * 32U + i);
        }
    }
    for (size_t i = 0U; i < sizeof(image); ++i) image[i] = payload(i);
    rig_generation = 1U; /* This fixture's bulk image is immutable. */
    protocol_init_error = iolink_phy_tiol112_init(&tiol112, board_tiol112_io());
    if (protocol_init_error != 0) {
        for (;;) {
        }
    }
    config.phy = *iolink_phy_tiol112_get(&tiol112);
    config.device_info = &identity;
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config.stack.pd_in_len = 32U;
    config.stack.pd_out_len = 1U;
    config.stack.od_len = 1U;
    config.stack.min_cycle_time = RIG_MIN_CYCLE;
    config.pd_input_select = select_block;
    config.vendor_service = image_service;
    protocol_init_error = iolink_device_init(&protocol_device, &config);
    if (protocol_init_error != 0) {
        for (;;) {
        }
    }
    (void) iolink_device_pd_input_update(&protocol_device, blocks[0], 32U, true);
    /* Legacy statistic ends at UART TC. This is a 20ms completion watchdog,
     * NOT a tA limit. Measure final master stop-end to first CQ start separately.
     * Inter-byte and other DLL checks remain enabled.
     */
    iolink_device_set_t_ren_limit_us(&protocol_device, 20000U);
    protocol_init_error = iolink_dll_set_baudrate(&protocol_device.dll, RIG_BAUD);
    if (protocol_init_error != 0) {
        for (;;) {
        }
    }
    protocol_ready = 1U;
    for (;;) {
        iolink_device_process(&protocol_device);
        if ((iolink_device_get_phy_mode(&protocol_device) == IOLINK_PHY_MODE_SIO) &&
            (iolink_device_get_baudrate(&protocol_device) != RIG_BAUD)) {
            (void) iolink_dll_set_baudrate(&protocol_device.dll, RIG_BAUD);
        }
        rig_phy_fault = iolink_phy_tiol112_fault(&tiol112);
        board_led((iolink_device_get_state(&protocol_device) == IOLINK_DLL_STATE_OPERATE) &&
                  (rig_phy_fault == 0));
    }
}
