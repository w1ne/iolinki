#include "iolinki/events.h"
#include <string.h>

#define EVENT_QUEUE_SIZE 8

static struct {
    iolink_event_t queue[EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} g_event_q;

void iolink_events_init(void)
{
    memset(&g_event_q, 0, sizeof(g_event_q));
}

void iolink_event_trigger(uint16_t code, iolink_event_type_t type)
{
    if (g_event_q.count >= EVENT_QUEUE_SIZE) {
        /* Drop oldest or ignore? Usually drop oldest in industrial stacks. */
        iolink_event_t dummy;
        iolink_events_pop(&dummy);
    }

    g_event_q.queue[g_event_q.tail].code = code;
    g_event_q.queue[g_event_q.tail].type = type;
    g_event_q.tail = (g_event_q.tail + 1) % EVENT_QUEUE_SIZE;
    g_event_q.count++;
}

bool iolink_events_pending(void)
{
    return (g_event_q.count > 0);
}

bool iolink_events_pop(iolink_event_t *event)
{
    if (g_event_q.count == 0) return false;

    *event = g_event_q.queue[g_event_q.head];
    g_event_q.head = (g_event_q.head + 1) % EVENT_QUEUE_SIZE;
    g_event_q.count--;
    return true;
}
