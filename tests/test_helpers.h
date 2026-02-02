#ifndef TEST_HELPERS_H_
#define TEST_HELPERS_H_

#include <stdint.h>
#include <stddef.h>
#include "iolinki/iolink.h"
#include "iolinki/phy.h"

/* Test buffers */
extern uint8_t g_tx_buf[1024];
extern uint8_t g_rx_buf[1024];

/* Mock PHY driver */
extern const iolink_phy_api_t g_phy_mock;

/* Helper to setup mock expectations */
void setup_mock_phy(void);
void iolink_phy_mock_reset(void);

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
extern const iolink_ds_storage_api_t g_ds_storage_mock;
void iolink_ds_mock_reset(void);
uint8_t* iolink_ds_mock_get_buf(void);

#endif  // TEST_HELPERS_H_
