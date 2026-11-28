/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PROTOCOL_RIG_H
#define IOLINK_PROTOCOL_RIG_H

/** Run byte-exact protocol regression cases; returns number of failed cases.
 *  This harness uses queued PHY octets and does not measure electrical tA. */
unsigned iolink_protocol_rig_run(void (*emit)(const char*));

#endif
