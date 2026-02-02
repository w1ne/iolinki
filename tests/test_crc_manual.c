#include <stdio.h>
#include <stdint.h>
#include "iolinki/crc.h"

int main() {
    uint8_t data[] = {0x00, 0x00};
    uint8_t val = iolink_crc6(data, 2);
    printf("C CRC(0x00, 0x00) = 0x%02X\n", val);
    
    uint8_t d2[] = {0x95, 0x00};
    printf("C CRC(0x95, 0x00) = 0x%02X\n", iolink_crc6(d2, 2));

    return 0;
}
