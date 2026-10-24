/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_EVENTS_H
#define IOLINK_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/config.h"

/**
 * @file events.h
 * @brief IO-Link Event Handling
 */

/**
 * @defgroup iolinki_events Event Handling
 * @brief Diagnostic event codes, severity levels and the Device event queue.
 * @{
 */

/**
 * @brief IO-Link Event Severity Levels
 */
typedef enum
{
    IOLINK_EVENT_TYPE_NOTIFICATION = 0U, /**< Information only, no action required */
    IOLINK_EVENT_TYPE_WARNING = 1U,      /**< Potential issue, operation continues */
    IOLINK_EVENT_TYPE_ERROR = 2U         /**< Critical failure, operation might be degraded */
} iolink_event_type_t;

/**
 * @brief Event MODE (A.6.4 / Figure A.24)
 *
 * Error and Warning events use the appears/disappears pair; Notification
 * events are reported once (single shot).
 */
typedef enum
{
    IOLINK_EVENT_MODE_APPEARS = 0U,    /**< Event appears (0b11). */
    IOLINK_EVENT_MODE_DISAPPEARS = 1U, /**< Event disappears (0b10). */
    IOLINK_EVENT_MODE_SINGLE_SHOT = 2U /**< Notification, reported once. */
} iolink_event_mode_t;

/** Maximum number of simultaneously active (appears/disappears) events. */
#ifndef IOLINK_EVENT_PENDING_MAX
#define IOLINK_EVENT_PENDING_MAX 8U
#endif

/*
 * Device vendor-specific communication events.
 *
 * Table D.1 of the IO-Link V1.1.5 specification reserves 0x1800-0x18FF for
 * vendor-specific Device events. These values coincide numerically with the
 * Master/Port codes of Table D.2 (e.g. 0x1805 = "Overtemperature" and
 * 0x1806 = "Short circuit at L+" on the port); the EventQualifier SOURCE bit
 * (Table A.18: 0 = Device) tells the two uses apart. Standardized device
 * conditions have dedicated Table D.1 codes - use the IOLINK_EVENTCODE_*
 * block below for those, never these vendor-specific values.
 */
#define IOLINK_EVENT_COMM_CRC 0x1801U     /**< CRC error in communication */
#define IOLINK_EVENT_COMM_TIMEOUT 0x1802U /**< Timeout in communication */
#define IOLINK_EVENT_COMM_FRAMING 0x1803U /**< Framing error in communication */
#define IOLINK_EVENT_COMM_TIMING 0x1804U  /**< Timing violation in communication */

/*
 * Standardized IO-Link EventCodes for Devices, per V1.1.5 Annex D, Table D.1.
 * The trailing comment gives the spec EventType. Vendor-specific Device events
 * use the ranges 0x1800-0x18FF and 0x8CA0-0x8DFF.
 */
#define IOLINK_EVENTCODE_NO_MALFUNCTION 0x0000U             /**< Notification */
#define IOLINK_EVENTCODE_GENERAL_MALFUNCTION 0x1000U        /**< Error */
#define IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD 0x4000U       /**< Error */
#define IOLINK_EVENTCODE_TEMPERATURE_OVERRUN 0x4210U        /**< Warning */
#define IOLINK_EVENTCODE_TEMPERATURE_UNDERRUN 0x4220U       /**< Warning */
#define IOLINK_EVENTCODE_HARDWARE_FAULT 0x5000U             /**< Error */
#define IOLINK_EVENTCODE_COMPONENT_MALFUNCTION 0x5010U      /**< Error */
#define IOLINK_EVENTCODE_NVM_LOSS 0x5011U                   /**< Error */
#define IOLINK_EVENTCODE_BATTERY_LOW 0x5012U                /**< Warning */
#define IOLINK_EVENTCODE_POWER_SUPPLY_FAULT 0x5100U         /**< Error */
#define IOLINK_EVENTCODE_FUSE_BLOWN 0x5101U                 /**< Error */
#define IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN 0x5110U     /**< Warning */
#define IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN 0x5111U    /**< Warning */
#define IOLINK_EVENTCODE_SOFTWARE_FAULT 0x6000U             /**< Error */
#define IOLINK_EVENTCODE_PARAMETER_ERROR 0x6320U            /**< Error */
#define IOLINK_EVENTCODE_PARAMETER_MISSING 0x6321U          /**< Error */
#define IOLINK_EVENTCODE_WIRE_BREAK 0x7700U                 /**< Error */
#define IOLINK_EVENTCODE_SHORT_CIRCUIT 0x7710U              /**< Error */
#define IOLINK_EVENTCODE_GROUND_FAULT 0x7711U               /**< Error */
#define IOLINK_EVENTCODE_APPLICATION_FAULT 0x8C00U          /**< Error */
#define IOLINK_EVENTCODE_SIMULATION_ACTIVE 0x8C01U          /**< Warning */
#define IOLINK_EVENTCODE_PV_RANGE_OVERRUN 0x8C10U           /**< Warning */
#define IOLINK_EVENTCODE_MEASUREMENT_RANGE_EXCEEDED 0x8C20U /**< Error */
#define IOLINK_EVENTCODE_PV_RANGE_UNDERRUN 0x8C30U          /**< Warning */
#define IOLINK_EVENTCODE_MAINTENANCE_CLEANING 0x8C40U       /**< Warning (maintenance) */
#define IOLINK_EVENTCODE_MAINTENANCE_REFILL 0x8C41U         /**< Warning (maintenance) */
#define IOLINK_EVENTCODE_MAINTENANCE_WEAR 0x8C42U           /**< Warning (maintenance) */
#define IOLINK_EVENTCODE_DS_UPLOAD_REQUEST 0xFF91U          /**< Notification (internal DS) */

/**
 * @brief Event Descriptor
 *
 * Represents a single IO-Link diagnostic event.
 */
typedef struct
{
    uint16_t code;            /**< 16-bit IO-Link EventCode (per spec or device-specific) */
    iolink_event_type_t type; /**< Severity level */
    iolink_event_mode_t mode; /**< appears / disappears / single shot */
} iolink_event_t;

/**
 * @brief Lifecycle tracking for one active Error/Warning EventCode.
 *
 * Implements the A.6.4 appears/disappears pairing: a code may only appear
 * once until its disappearance has been reported, and a disappearance is not
 * issued within IOLINK_EVENT_MIN_APPEAR_MS of the appearance.
 */
typedef struct
{
    uint16_t code;              /**< EventCode */
    iolink_event_type_t type;   /**< Severity */
    uint32_t appear_ms;         /**< Time the event appeared */
    uint32_t last_tx_ms;        /**< Last time the same code/mode was queued */
    bool clear_pending;         /**< Disappearance requested, waiting for the 50 ms window */
    bool active;                /**< Slot in use */
} iolink_event_pending_t;

/**
 * @brief Events Engine Context
 *
 * Manages the internal queue (ordered by severity) of pending events and the
 * Table 58 event memory exposed on the diagnosis communication channel.
 */
typedef struct
{
    iolink_event_t queue[IOLINK_EVENT_QUEUE_SIZE]; /**< Severity-ordered event buffer */
    uint8_t count;                                 /**< Number of events currently in queue */

    iolink_event_pending_t pending[IOLINK_EVENT_PENDING_MAX]; /**< Active appears events */
    uint8_t pending_count;                                    /**< Used pending slots */

    /* Table 58 event memory: StatusCode(1) + 6 slots * (Qualifier, CodeMSB, CodeLSB). */
    uint8_t memory[19];    /**< Event memory image (0x00-0x12) */
    bool frozen;           /**< Memory frozen while the Event flag is set (Table 60) */
    bool flag;             /**< Event flag (CKS bit 7) */
    uint8_t frozen_slots;  /**< Number of memory slots populated when the memory was frozen. */
} iolink_events_ctx_t;

/**
 * @brief Initialize the event engine
 *
 * Resets the queue and internal counters.
 *
 * @param ctx Event context to initialize
 */
void iolink_events_init(iolink_events_ctx_t* ctx);

/**
 * @brief Trigger a new diagnostic event
 *
 * Adds an event to the internal queue. If the queue is full, the newest event
 * is typically dropped or the oldest overwritten (depending on implementation).
 *
 * @param ctx Event context
 * @param code 16-bit IO-Link EventCode
 * @param type Severity level
 */
void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type);

/**
 * @brief Report that a previously triggered Error/Warning event disappeared.
 *
 * A.6.4: the disappearance is suppressed when the code is not currently
 * active, and is deferred until IOLINK_EVENT_MIN_APPEAR_MS has elapsed since
 * the appearance (checked by iolink_events_process()).
 *
 * @param ctx Event context
 * @param code EventCode that disappeared
 */
void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code);

/**
 * @brief Service deferred disappearances (call periodically from the main loop).
 *
 * Emits the "Event disappears" for any pending code whose 50 ms minimum
 * appearance window has elapsed.
 *
 * @param ctx Event context
 */
void iolink_events_process(iolink_events_ctx_t* ctx);

/**
 * @brief Discard all queued events and reset the lifecycle tracking.
 *
 * A.6.4: events in the queue are discarded when communication is interrupted
 * or cancelled. Also clears the event memory and flag.
 *
 * @param ctx Event context
 */
void iolink_events_reset(iolink_events_ctx_t* ctx);

/**
 * @brief Check if any events are pending for Master retrieval
 *
 * @param ctx Event context
 * @return true if one or more events are in the queue
 */
bool iolink_events_pending(const iolink_events_ctx_t* ctx);

/**
 * @brief Read the Event flag sent in the CKS bit 7 (7.3.8.2).
 *
 * The flag is set when an event has been written to the event memory and is
 * cleared by a master write to StatusCode (event memory address 0).
 *
 * @param ctx Event context
 * @return true while the Event flag is set
 */
bool iolink_events_flag(const iolink_events_ctx_t* ctx);

/**
 * @brief Read one octet of the Table 58 event memory (diagnosis channel).
 *
 * Address 0 is the StatusCode type 2 (Figure A.22): bit 7 Event Details = 1 and
 * bits 0-5 one bit per active slot 1..6. Slot n occupies addresses 3n-2
 * (EventQualifier), 3n-1 (EventCode MSB) and 3n (EventCode LSB).
 *
 * @param ctx Event context
 * @param addr Event memory address (0x00-0x12)
 * @return uint8_t Memory octet, 0 for an out-of-range address
 */
uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr);

/**
 * @brief Write one octet of the Table 58 event memory (diagnosis channel).
 *
 * A write to address 0 (StatusCode) confirms the event readout: the Event flag
 * is cleared and the memory is released/marked invalid and rebuilt from any
 * events that remain queued (Table 60 T5). Writes to other addresses are
 * ignored.
 *
 * @param ctx Event context
 * @param addr Event memory address
 * @param value Value written by the master (ignored, 7.3.8.2)
 */
void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value);

/**
 * @brief Pop the oldest event from the queue
 *
 * Typically called by the ISDU engine when satisfying a read request for Index 2.
 *
 * @param ctx Event context
 * @param event [out] Pointer to store the popped event details
 * @return true if an event was successfully popped, false if queue was empty
 */
bool iolink_events_pop(iolink_events_ctx_t* ctx, iolink_event_t* event);

/**
 * @brief Peek at the oldest event without removing it from the queue
 *
 * Used for OD event content to check the next event code.
 *
 * @param ctx Event context
 * @param event [out] Pointer to store the event details
 * @return true if an event was available, false if queue was empty
 */
bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event);

/**
 * @brief Get the highest severity level currently in the event queue
 *
 * Maps to IO-Link Device Status per Table B.13 (0=OK, 1=Maintenance,
 * 2=Out of Spec, 3=Functional-Check, 4=Failure). The lowest priority value
 * dominates: Failure (4) beats Functional-Check (3), which beats Maintenance
 * (1) and Out of Spec (2).
 *
 * @param ctx Event context
 * @return uint8_t Highest severity level (0-4)
 */
uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t* ctx);

/**
 * @brief Copy all pending events to a buffer without popping them
 *
 * @param ctx Event context
 * @param out_events [out] Buffer to store event copies
 * @param max_count Maximum number of events to copy
 * @return uint8_t Number of events copied
 */
uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count);

/**
 * @brief Return the EventType the V1.1.5 spec defines for a standard EventCode.
 *
 * For standardized codes (Annex D, Table D.1) this returns the spec-defined
 * notification/warning/error classification. For reserved or unknown codes it
 * returns IOLINK_EVENT_TYPE_ERROR as a safe default.
 *
 * @param code 16-bit EventCode
 * @return iolink_event_type_t Spec EventType
 */
iolink_event_type_t iolink_event_classify(uint16_t code);

/** @} */ /* end of iolinki_events */

#endif  // IOLINK_EVENTS_H
