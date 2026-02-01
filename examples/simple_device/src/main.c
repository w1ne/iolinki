#include <stdio.h>
#include "iolinki/iolink.h"

int main(void) {
    printf("Starting IO-Link Device...\n");
    if (iolink_init() == 0) {
        printf("IO-Link stack initialized successfully.\n");
    } else {
        printf("Failed to initialize IO-Link stack.\n");
    }
    return 0;
}
