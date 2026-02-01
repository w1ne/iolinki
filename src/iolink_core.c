#include "iolinki/iolink.h"
#include <stddef.h>

static const iolink_phy_api_t *g_phy = NULL;

int iolink_init(const iolink_phy_api_t *phy)
{
    if (phy == NULL) {
        return -1;
    }

    g_phy = phy;

    if (g_phy->init) {
        return g_phy->init();
    }

    return 0;
}
