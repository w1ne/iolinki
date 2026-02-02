#include "iolinki/events.h"
#include <string.h>

void iolink_events_init(iolink_events_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(iolink_events_ctx_t));
    }
}

void iolink_event_trigger(iolink_events_ctx_t *ctx, uint16_t code, iolink_event_type_t type)
{
    if (!ctx) return;

    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        /* Drop oldest */
        iolink_event_t dummy;
        iolink_events_pop(ctx, &dummy);
    }

    ctx->queue[ctx->tail].code = code;
    ctx->queue[ctx->tail].type = type;
    ctx->tail = (uint8_t)((ctx->tail + 1) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count++;
}

bool iolink_events_pending(iolink_events_ctx_t *ctx)
{
    return (ctx && ctx->count > 0);
}

bool iolink_events_pop(iolink_events_ctx_t *ctx, iolink_event_t *event)
{
    if (!ctx || ctx->count == 0) return false;

    *event = ctx->queue[ctx->head];
    ctx->head = (uint8_t)((ctx->head + 1) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count--;
    return true;
}
