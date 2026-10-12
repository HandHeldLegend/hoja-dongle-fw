#ifndef FLOSS_CONFIG_DONGLE_H
#define FLOSS_CONFIG_DONGLE_H

/*
 * FLOSS configuration for the dongle (FLOSS_USER_CONFIG_FILE). Only what differs from
 * floss_config.h.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * SPDX-License-Identifier: MIT-0
 */

/* One gamepad, plus room for a second one calling while the first link closes */
#define FLOSS_MAX_CONNECTIONS 2

/* The CYW43 shared-bus transport writes a 4-byte header in front of each packet */
#define FLOSS_TRANSPORT_MAX_HEADROOM 4

/* Classic link profile only */
#define FLOSS_ENABLE_LE     0
#define FLOSS_ENABLE_RFCOMM 0
#define FLOSS_ENABLE_LINK   1

/* A live report is a length byte plus up to 64 bytes (dongle_link.h); the gamepad uses the same */
#define FLOSS_LINK_MAX_REPORT 65

#define FLOSS_LOG_LEVEL     FLOSS_LOG_WARN
#define FLOSS_TRACE_ENTRIES 0
#define FLOSS_ENABLE_STATS  0
#define FLOSS_ENABLE_SNOOP  0

#endif
