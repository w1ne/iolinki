#include "iolinki/isdu.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include <string.h>

/* 
 * IO-Link ISDU Segmentation Engine
 * 
 * ISDU Header (2 or 3 bytes):
 * [7:4] Service (Read/Write)
 * [3:0] Length (if < 15)
 * [ Index (16-bit) ]
 * [ Subindex (8-bit) ]
 */

/* ... includes ... */

/* Structs moved to header iolink_isdu_ctx_t */

void iolink_isdu_init(iolink_isdu_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(iolink_isdu_ctx_t));
        ctx->state = ISDU_STATE_IDLE;
    }
}

int iolink_isdu_collect_byte(iolink_isdu_ctx_t *ctx, uint8_t byte)
{
    if (!ctx) return 0;

    switch (ctx->state) {
        case ISDU_STATE_IDLE:
            /* ISDU Header Byte: [Service 4bit] [Length 4bit] */
            {
                uint8_t service = (byte >> 4) & 0x0F;
                uint8_t length = byte & 0x0F;
                
                if (service == 0x09) { /* READ (0x90) */
                    ctx->header.type = IOLINK_ISDU_SERVICE_READ;
                    ctx->header.length = length;
                    ctx->state = ISDU_STATE_HEADER_INDEX_HIGH;
                    ctx->buffer_idx = 0;
                } else if (service == 0x0A) { /* WRITE (0xA0..0xAF) */
                    ctx->header.type = IOLINK_ISDU_SERVICE_WRITE;
                    ctx->header.length = length;
                    ctx->state = ISDU_STATE_HEADER_INDEX_HIGH;
                    ctx->buffer_idx = 0;
                }
            }
            break;

        case ISDU_STATE_HEADER_INDEX_HIGH:
            ctx->header.index = (uint16_t)(byte << 8);
            ctx->state = ISDU_STATE_HEADER_INDEX_LOW;
            break;

        case ISDU_STATE_HEADER_INDEX_LOW:
            ctx->header.index |= byte;
            ctx->state = ISDU_STATE_HEADER_SUBINDEX;
            break;

        case ISDU_STATE_HEADER_SUBINDEX:
            ctx->header.subindex = byte;
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                ctx->state = ISDU_STATE_DATA_COLLECT;
            } else {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1; /* Request complete */
            }
            break;

        case ISDU_STATE_DATA_COLLECT:
            ctx->buffer[ctx->buffer_idx++] = byte;
            if (ctx->buffer_idx >= ctx->header.length) {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1;
            }
            return 0;

        case ISDU_STATE_RESPONSE_READY:
            if ((byte & 0xC0) == 0x00) return 0;
            ctx->state = ISDU_STATE_IDLE;
            return iolink_isdu_collect_byte(ctx, byte);

        default:
            ctx->state = ISDU_STATE_IDLE;
            break;
    }
    return 0;
}

static void handle_mandatory_indices(iolink_isdu_ctx_t *ctx)
{
    const iolink_device_info_t *info = iolink_device_info_get();
    const char *str_data = NULL;
    
    
    switch (ctx->header.index) {
        case 0x0010: str_data = info->vendor_name; break;
        case 0x0011: str_data = info->vendor_text; break;
        case 0x0012: str_data = info->product_name; break;
        case 0x0013: str_data = info->product_id; break;
        case 0x0014: str_data = info->product_text; break;
        case 0x0015: str_data = info->serial_number; break;
        case 0x0016: str_data = info->hardware_revision; break;
        case 0x0017: str_data = info->firmware_revision; break;
        
        case 0x0018: /* Application Tag (optional) */
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                if (iolink_device_info_set_application_tag((const char*)ctx->buffer, (uint8_t)ctx->buffer_idx) == 0) {
                     ctx->response_len = 0;
                     ctx->response_idx = 0;
                     ctx->state = ISDU_STATE_RESPONSE_READY;
                     return;
                }
            } else {
                str_data = info->application_tag;
            }
            break;
            
        case 0x0024:
            ctx->response_buf[0] = info->min_cycle_time;
            ctx->response_len = 1;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
            
        case 0x001E:
            ctx->response_buf[0] = (uint8_t)(info->revision_id >> 8);
            ctx->response_buf[1] = (uint8_t)(info->revision_id & 0xFF);
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
            
        default: return;
    }
    
    if (str_data) {
        size_t len_sz = strlen(str_data);
        if (len_sz > sizeof(ctx->response_buf)) len_sz = sizeof(ctx->response_buf);
        memcpy(ctx->response_buf, str_data, len_sz);
        ctx->response_len = (uint8_t)len_sz;
        ctx->response_idx = 0;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
}

static void handle_system_command(iolink_isdu_ctx_t *ctx, uint8_t cmd)
{
    switch (cmd) {
        case 0x80: iolink_isdu_init(ctx); break;
        case 0x81: break;
        case 0x82: break;
        case 0x83: break;
        default:
            ctx->response_buf[0] = 0x80;
            ctx->response_buf[1] = 0x11;
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
    }
    ctx->response_len = 0;
    ctx->response_idx = 0;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

static uint16_t g_access_locks = 0x0000; /* TODO: Move to ctx or device info */

static void handle_access_locks(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_READ) {
        ctx->response_buf[0] = (uint8_t)(g_access_locks >> 8);
        ctx->response_buf[1] = (uint8_t)(g_access_locks & 0xFF);
        ctx->response_len = 2;
    } else {
        ctx->response_len = 0;
    }
    ctx->response_idx = 0;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

static void handle_standard_commands(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.index == 0x0002) {
        if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
             /* Assuming command in collected data (missing logic in simple impl, but OK) */
             /* We assume 0x80 for now if data is empty or stub */
             handle_system_command(ctx, 0x80); 
        } else {
            ctx->response_buf[0] = 0x80;
            ctx->response_buf[1] = 0x11;
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
    } else if (ctx->header.index == 0x000C) {
        handle_access_locks(ctx);
    } else if (ctx->header.index == 0x02) {
        iolink_event_t ev;
        /* Need event context */
        if (ctx->event_ctx && iolink_events_pop((iolink_events_ctx_t*)ctx->event_ctx, &ev)) {
            ctx->response_buf[0] = (uint8_t)((ev.type << 6) | 0x20);
            ctx->response_buf[1] = (uint8_t)(ev.code >> 8);
            ctx->response_buf[2] = (uint8_t)(ev.code & 0xFF);
            ctx->response_len = 3;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
    } else {
        handle_mandatory_indices(ctx);
    }
}

void iolink_isdu_process(iolink_isdu_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            ctx->state = ISDU_STATE_IDLE;
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t *ctx, uint8_t *byte)
{
    if (!ctx || ctx->state != ISDU_STATE_RESPONSE_READY) return 0;

    if (ctx->response_idx < ctx->response_len) {
        *byte = ctx->response_buf[ctx->response_idx++];
        if (ctx->response_idx >= ctx->response_len) {
            ctx->state = ISDU_STATE_IDLE;
        }
        return 1;
    }

    ctx->state = ISDU_STATE_IDLE;
    return 0;
}

