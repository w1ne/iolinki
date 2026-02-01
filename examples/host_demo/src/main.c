#include <stdio.h>
#include <unistd.h>
#include "iolinki/iolink.h"
#include "iolinki/phy_virtual.h"

int main(void)
{
    printf("=== iolinki Host Demo ===\n");
    printf("IO-Link Device Stack v0.1.0\n\n");

    /* Initialize stack with virtual PHY */
    const iolink_phy_api_t *phy = iolink_phy_virtual_get();
    
    if (iolink_init(phy) != 0) {
        printf("ERROR: Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("Stack initialized successfully\n");
    printf("Running protocol state machine...\n\n");

    /* Simulate periodic processing and PD exchange */
    uint8_t pd_in = 0;
    for (int i = 0; i < 20; i++) {
        /* Update input data (Device -> Master) */
        pd_in++;
        iolink_pd_input_update(&pd_in, 1);
        
        printf("Cycle %d: Processing stack... PD IN: %02X\n", i, pd_in);
        iolink_process();
        
        /* Check for output data (Master -> Device) */
        uint8_t pd_out;
        if (iolink_pd_output_read(&pd_out, 1) > 0) {
            printf("  Master Output received: %02X\n", pd_out);
        }
        
        usleep(50000); /* 50ms */
    }

    printf("\nDemo completed successfully\n");
    return 0;
}
