#ifndef TEST_HELPERS_H_
#define TEST_HELPERS_H_

#include <stdint.h>
#include <stddef.h>
#include "iolinki/iolink.h"

/* Test buffers */
extern uint8_t g_tx_buf[1024];
extern uint8_t g_rx_buf[1024];

/* Mock PHY operations */
typedef struct {
    int (*send_byte)(uint8_t byte);
    int (*recv_byte)(uint8_t *byte);
    void (*set_mode)(int mode);
} mock_phy_ops_t;

extern mock_phy_ops_t g_mock_phy;

/* Mock timer functions */
uint32_t mock_get_time_ms(void);
void mock_delay_ms(uint32_t ms);

#endif  // TEST_HELPERS_H_
