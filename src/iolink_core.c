#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include <stddef.h>

static iolink_dll_ctx_t g_dll_ctx;

int iolink_init(const iolink_phy_api_t *phy)
{
    if (phy == NULL) {
        return -1;
    }

    if (phy->init) {
        int err = phy->init();
        if (err != 0) return err;
    }

    iolink_dll_init(&g_dll_ctx, phy);

    return 0;
}

void iolink_process(void)
{
    iolink_dll_process(&g_dll_ctx);
}
