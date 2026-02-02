#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <cmocka.h>
#include <string.h>
#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/application.h"
#include "iolinki/phy.h"

/* Mock PHY for PD testing */
static uint8_t g_last_sent[64];
static uint8_t g_last_sent_len = 0;

static int mock_send(const uint8_t *data, size_t len) {
    memcpy(g_last_sent, data, len);
    g_last_sent_len = (uint8_t)len;
    return 0;
}

static uint8_t g_recv_buf[64];
static uint8_t g_recv_idx = 0;
static uint8_t g_recv_len = 0;

static int mock_recv(uint8_t *byte) {
    if (g_recv_idx < g_recv_len) {
        *byte = g_recv_buf[g_recv_idx++];
        return 1;
    }
    return 0;
}

static const iolink_phy_api_t g_mock_phy = {
    .init = NULL,
    .set_baudrate = NULL,
    .send = mock_send,
    .recv_byte = mock_recv
};

static void test_pd_variable_lengths(void **state) {
    (void)state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 2,
        .pd_out_len = 2
    };
    
    iolink_init(&g_mock_phy, &config);
    
    /* 1. Simulate Transition to OPERATE */
    g_recv_buf[0] = 0x0F; /* MC: Transition */
    g_recv_buf[1] = 0xC1; /* CK for MC=0x0F (Type 0 checksum) */
    g_recv_len = 2;
    g_recv_idx = 0;
    
    iolink_process();
    
    /* 2. Verify 2-byte PD Update */
    uint8_t input_pd[2] = {0xAA, 0xBB};
    iolink_pd_input_update(input_pd, 2, true);
    
    /* 3. Send a Type 1_1 Frame (MC + CKT + PD_OUT(2) + OD + CK) */
    /* Master -> Device: MC=0x80 (Read Index 0), CKT=0x00, PD_OUT={0x11, 0x22}, OD=0x00, CK=?? */
    uint8_t frame[] = {0x80, 0x00, 0x11, 0x22, 0x00, 0x00};
    /* CRC6 calculation for {0x80, 0x00, 0x11, 0x22, 0x00} */
    /* We can cheat or use iolink_crc6 from the library if available in test */
    extern uint8_t iolink_crc6(const uint8_t *data, size_t len);
    frame[5] = iolink_crc6(frame, 5);
    
    memcpy(g_recv_buf, frame, 6);
    g_recv_len = 6;
    g_recv_idx = 0;
    
    iolink_process();
    
    /* 4. Verify Response: Status + PD_IN(2) + OD + CK */
    /* Status should have bit 5 set (PDStatus=1) */
    assert_true(g_last_sent_len == 5);
    assert_true(g_last_sent[0] & 0x20); /* Valid bit */
    assert_int_equal(g_last_sent[1], 0xAA);
    assert_int_equal(g_last_sent[2], 0xBB);
}

static void test_pd_invalid_flag(void **state) {
    (void)state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 1,
        .pd_out_len = 1
    };
    iolink_init(&g_mock_phy, &config);
    
    /* Transition to OPERATE */
    g_recv_buf[0] = 0x0F; g_recv_buf[1] = 0xC1;
    g_recv_len = 2; g_recv_idx = 0;
    iolink_process();
    
    /* Set PD as INVALID */
    uint8_t data = 0x55;
    iolink_pd_input_update(&data, 1, false);
    
    /* Master Request: MC=0x80, CKT=0x00, PD_OUT=0x00, OD=0x00, CK=?? */
    uint8_t frame[] = {0x80, 0x00, 0x00, 0x00, 0x00};
    extern uint8_t iolink_crc6(const uint8_t *data, size_t len);
    frame[4] = iolink_crc6(frame, 4);
    
    memcpy(g_recv_buf, frame, 5);
    g_recv_len = 5; g_recv_idx = 0;
    iolink_process();
    
    /* Verify Status bit 5 is 0 */
    assert_true(g_last_sent_len == 4);
    assert_false(g_last_sent[0] & 0x20); /* PDStatus bit should be 0 */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_variable_lengths),
        cmocka_unit_test(test_pd_invalid_flag),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
