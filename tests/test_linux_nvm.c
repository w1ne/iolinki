/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char* name = "iolink_nvm.bin";
    (void) remove(name);
    const uint8_t first[] = {1, 2, 3, 4};
    const uint8_t replacement[] = {5, 6};
    uint8_t actual[4];
    assert(iolink_nvm_write(0, first, sizeof(first)) == 0);
    assert(iolink_nvm_write(1, replacement, sizeof(replacement)) == 0);
    assert(iolink_nvm_read(0, actual, sizeof(actual)) == 0);
    const uint8_t expected[] = {1, 5, 6, 4};
    assert(memcmp(actual, expected, sizeof(expected)) == 0);
    assert(remove(name) == 0);
    return 0;
}
