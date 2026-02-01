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

    /* Simulate periodic processing */
    for (int i = 0; i < 10; i++) {
        printf("Cycle %d: Processing stack...\n", i);
        iolink_process();
        usleep(100000); /* 100ms */
    }

    printf("\nDemo completed successfully\n");
    return 0;
}
