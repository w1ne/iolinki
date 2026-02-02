#include "iolinki/device_info.h"
#include <string.h>

static const iolink_device_info_t *g_device_info = NULL;

/* Default device info (can be overridden by application) */
static iolink_device_info_t g_default_info = {
    .vendor_name = "iolinki",
    .vendor_text = "Open-Source IO-Link Stack",
    .product_name = "Generic IO-Link Device",
    .product_id = "IOLINK-DEV-001",
    .product_text = "Reference Implementation",
    .serial_number = "0000000001",
    .hardware_revision = "1.0",
    .firmware_revision = "0.1.0",
    .application_tag = "DefaultTag",
    
    .vendor_id = 0xFFFF,        /* Unassigned vendor ID */
    .device_id = 0x00000001,
    .function_id = 0x0000,
    
    .min_cycle_time = 10,       /* 1.0ms (10 * 100μs) */
    .revision_id = 0x0001
};

static char g_app_tag_buffer[33] = "DefaultTag";

void iolink_device_info_init(const iolink_device_info_t *info)
{
    /* If user provides info, we use it (const). */
    /* Note: Writing to app tag when using user-provided const info will fail or require separate handling. */
    /* For now, we assume default info or shallow copy if needed. */
    if (info) {
        /* Shallow copy to internal non-const struct to allow modification of pointers? */
        /* Or just update the global pointer. */
        g_device_info = info;
    } else {
        g_device_info = &g_default_info;
        g_default_info.application_tag = g_app_tag_buffer;
    }
}

int iolink_device_info_set_application_tag(const char *tag, uint8_t len)
{
    if (len >= sizeof(g_app_tag_buffer)) return -1;
    
    /* If we are using g_default_info, we can update the buffer */
    if (g_device_info == &g_default_info) {
        memcpy(g_app_tag_buffer, tag, len);
        g_app_tag_buffer[len] = '\0';
        g_default_info.application_tag = g_app_tag_buffer;
        return 0;
    }
    return -1; /* Cannot update read-only user info */
}

const iolink_device_info_t* iolink_device_info_get(void)
{
    if (!g_device_info) {
        g_device_info = &g_default_info;
    }
    return g_device_info;
}
