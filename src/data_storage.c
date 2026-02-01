#include "iolinki/data_storage.h"
#include <string.h>
#include <stdio.h>

static struct {
    iolink_ds_state_t state;
    const iolink_ds_storage_api_t *storage;
    uint16_t current_checksum;
    uint16_t master_checksum;
} g_ds;

void iolink_ds_init(const iolink_ds_storage_api_t *storage)
{
    memset(&g_ds, 0, sizeof(g_ds));
    g_ds.storage = storage;
    g_ds.state = IOLINK_DS_STATE_IDLE;
}

uint16_t iolink_ds_calc_checksum(const uint8_t *data, size_t len)
{
    /* Fletcher-16 or simple sum for demo. IO-Link usually uses a specific CRC. */
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (size_t i = 0; i < len; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (uint16_t)((sum2 << 8) | sum1);
}

void iolink_ds_check(uint16_t master_checksum)
{
    g_ds.master_checksum = master_checksum;
    
    if (g_ds.state != IOLINK_DS_STATE_IDLE) return;

    if (master_checksum == 0) {
        /* Master has no data -> Upload request */
        g_ds.state = IOLINK_DS_STATE_UPLOAD_REQ;
    } else if (master_checksum != g_ds.current_checksum) {
        /* Checksum mismatch -> Download request (Update device) */
        g_ds.state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
}

void iolink_ds_process(void)
{
    switch (g_ds.state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
            /* Master indicated it has no data -> Device sends parameters */
            printf("[DS] Starting Upload to Master (Checksum: %04X)\n", g_ds.current_checksum);
            /* Byte-by-byte transfer would happen here */
            g_ds.state = IOLINK_DS_STATE_UPLOADING;
            break;

        case IOLINK_DS_STATE_UPLOADING:
            /* Complete upload simulation */
            g_ds.state = IOLINK_DS_STATE_IDLE;
            printf("[DS] Upload Complete\n");
            break;
            
        case IOLINK_DS_STATE_DOWNLOAD_REQ:
            /* Master indicated a mismatch -> Device receives parameters */
            printf("[DS] Starting Download from Master (Master CS: %04X)\n", g_ds.master_checksum);
            g_ds.state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_DS_STATE_DOWNLOADING:
            /* Update local parameters and storage */
            g_ds.current_checksum = g_ds.master_checksum;
            g_ds.state = IOLINK_DS_STATE_IDLE;
            printf("[DS] Download Complete, New Checksum: %04X\n", g_ds.current_checksum);
            break;

        default:
            break;
    }
}
