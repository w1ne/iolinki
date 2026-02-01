#ifndef IOLINK_EVENTS_H
#define IOLINK_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file events.h
 * @brief IO-Link Event Handling
 */

typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING      = 1,
    IOLINK_EVENT_TYPE_ERROR        = 2
} iolink_event_type_t;

typedef struct {
    uint16_t code;
    iolink_event_type_t type;
} iolink_event_t;

/**
 * @brief Initialize event engine
 */
void iolink_events_init(void);

/**
 * @brief Trigger a new event
 * @param code 16-bit IO-Link EventCode
 * @param type severity level
 */
void iolink_event_trigger(uint16_t code, iolink_event_type_t type);

/**
 * @brief Check if any events are pending for Master reading
 * @return true if events are in queue, false otherwise
 */
bool iolink_events_pending(void);

/**
 * @brief Pop oldest event for ISDU Index 2 reading
 * @param event Pointer to store event details
 * @return true if event popped, false if queue empty
 */
bool iolink_events_pop(iolink_event_t *event);

#endif // IOLINK_EVENTS_H
