#include "iolinki/isdu.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
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

typedef enum {
    ISDU_STATE_IDLE,
    ISDU_STATE_HEADER_INDEX,
    ISDU_STATE_HEADER_SUBINDEX,
    ISDU_STATE_DATA_COLLECT,
    ISDU_STATE_SERVICE_EXECUTE,
    ISDU_STATE_RESPONSE_READY
} isdu_state_t;

typedef struct {
    isdu_state_t state;
    uint8_t buffer[256];
    size_t buffer_idx;
    iolink_isdu_header_t header;
    uint8_t response_buf[256];
    size_t response_idx;
    size_t response_len;
} isdu_ctx_t;

static isdu_ctx_t g_isdu;

void iolink_isdu_init(void)
{
    memset(&g_isdu, 0, sizeof(g_isdu));
    g_isdu.state = ISDU_STATE_IDLE;
}

int iolink_isdu_collect_byte(uint8_t byte)
{
    /* 
     * Simplified 1-byte segmentation assembly.
     * In a real stack, we handle the segmentation control bits (Last, First).
     */
    switch (g_isdu.state) {
        case ISDU_STATE_IDLE:
            g_isdu.header.type = (byte & 0x80) ? IOLINK_ISDU_SERVICE_WRITE : IOLINK_ISDU_SERVICE_READ;
            g_isdu.state = ISDU_STATE_HEADER_INDEX;
            g_isdu.buffer_idx = 0;
            break;

        case ISDU_STATE_HEADER_INDEX:
            g_isdu.header.index = byte; /* Simplified 8-bit index for demo */
            g_isdu.state = ISDU_STATE_HEADER_SUBINDEX;
            break;

        case ISDU_STATE_HEADER_SUBINDEX:
            g_isdu.header.subindex = byte;
            g_isdu.state = ISDU_STATE_SERVICE_EXECUTE;
            return 1; /* Request complete */

        default:
            g_isdu.state = ISDU_STATE_IDLE;
            break;
    }
    return 0;
}

static void handle_standard_commands(void)
{
    if (g_isdu.header.index == 0x02) {
        /* System Event Read */
        iolink_event_t ev;
        if (iolink_events_pop(&ev)) {
            g_isdu.response_buf[0] = (uint8_t)((ev.type << 6) | 0x20); /* Qualifier */
            g_isdu.response_buf[1] = (uint8_t)(ev.code >> 8);
            g_isdu.response_buf[2] = (uint8_t)(ev.code & 0xFF);
            g_isdu.response_len = 3;
        } else {
            g_isdu.response_buf[0] = 0; /* No event */
            g_isdu.response_len = 1;
        }
    } else if (g_isdu.header.index == 0x10) {
        const char *name = "iolinki-project";
        g_isdu.response_len = strlen(name);
        memcpy(g_isdu.response_buf, name, g_isdu.response_len);
    } else {
        /* Error response placeholder */
        g_isdu.response_buf[0] = 0xFF;
        g_isdu.response_len = 1;
    }
    g_isdu.response_idx = 0;
    g_isdu.state = ISDU_STATE_RESPONSE_READY;
}

int iolink_isdu_get_response_byte(uint8_t *byte)
{
    if (g_isdu.state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands();
    }

    if (g_isdu.state == ISDU_STATE_RESPONSE_READY) {
        if (g_isdu.response_idx < g_isdu.response_len) {
            *byte = g_isdu.response_buf[g_isdu.response_idx++];
            if (g_isdu.response_idx >= g_isdu.response_len) {
                g_isdu.state = ISDU_STATE_IDLE;
            }
            return 1;
        }
    }

    return 0;
}
