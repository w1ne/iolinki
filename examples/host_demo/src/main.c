#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "iolinki/iolink.h"
#include "iolinki/phy_virtual.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <tty_device> [m_seq_type] [pd_len]\n", argv[0]);
        printf("  m_seq_type: 0 (default), 1 (Type 1_2), 2 (Type 2_2)\n");
        return -1;
    }

    printf("=== iolinki Host Demo ===\n");
    printf("IO-Link Device Stack v0.1.0\n");
    printf("Connecting to: %s\n", argv[1]);
    
    /* Parse optional config */
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0,
        .min_cycle_time = 0,
        .pd_in_len = 0,
        .pd_out_len = 0
    };

    if (argc >= 3) {
        int type = atoi(argv[2]);
        if (type == 1) {
            config.m_seq_type = IOLINK_M_SEQ_TYPE_1_2; /* Using 1_2 (Interleaved ISDU) */
            printf("Config: Type 1_2 (PD + OD 1 byte)\n");
        } else if (type == 2) {
            config.m_seq_type = IOLINK_M_SEQ_TYPE_2_2;
            printf("Config: Type 2_2 (PD + OD 2 bytes)\n");
        }
    }
    
    if (argc >= 4) {
        int len = atoi(argv[3]);
        config.pd_in_len = (uint8_t)len;
        config.pd_out_len = (uint8_t)len;
        printf("Config: PD Len %d bytes\n", len);
    } else if (config.m_seq_type != IOLINK_M_SEQ_TYPE_0) {
        /* Default PD len if not Type 0 */
        config.pd_in_len = 2;
        config.pd_out_len = 2;
        printf("Config: PD Len 2 bytes (default)\n");
    }
    
    printf("\n");

    /* Configure virtual PHY port */
    iolink_phy_virtual_set_port(argv[1]);

    /* Initialize stack with virtual PHY and config */
    const iolink_phy_api_t *phy = iolink_phy_virtual_get();
    
    if (iolink_init(phy, &config) != 0) {
        printf("ERROR: Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("Stack initialized successfully\n");
    printf("Running protocol state machine...\n\n");

    /* Simulate periodic processing and PD exchange */
    
    
    while (1) {
        /* Update input data (Device -> Master) */
        // printf("Cycle %d: Processing stack... PD IN: %02X\n", i, pd_in);
        
        iolink_process();
        
        /* Update input data (Device -> Master) */
        
        iolink_process();
        
        /* Check for output data (Master -> Device) */
        uint8_t pd_buffer[32];
        int len = iolink_pd_output_read(pd_buffer, sizeof(pd_buffer));
        if (len > 0) {
            /* Echo + 1 */
            for (int i=0; i<len; i++) {
                pd_buffer[i]++;
            }
            iolink_pd_input_update(pd_buffer, (size_t)len, true);
        }
        
        /* 1ms cycle time simulation */
        /* Note: For accurate timing, use a timer or sleep less */
        usleep(1000); /* 1ms polling to reduce CPU load */
    }

    return 0;
}
