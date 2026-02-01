#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include <string.h>

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
    iolink_ds_init(NULL); /* Storage hooks optional for now */

    return 0;
}

void iolink_process(void)
{
    iolink_dll_process(&g_dll_ctx);
    iolink_ds_process();
}

int iolink_pd_input_update(const uint8_t *data, size_t len)
{
    if (len > sizeof(g_dll_ctx.pd_in)) return -1;
    memcpy(g_dll_ctx.pd_in, data, len);
    g_dll_ctx.pd_in_len = (uint8_t)len;
    return 0;
}

int iolink_pd_output_read(uint8_t *data, size_t len)
{
    uint8_t read_len = (len < g_dll_ctx.pd_out_len) ? (uint8_t)len : g_dll_ctx.pd_out_len;
    memcpy(data, g_dll_ctx.pd_out, read_len);
    return (int)read_len;
}
