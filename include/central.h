/*
 * Centralized core-safe functions for managing dongle memory.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file central.h
 * @brief Public API for the central memory manager.
 */

#ifndef CENTRAL_H
#define CENTRAL_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_status_rumble(uint8_t left, uint8_t right, uint8_t brake_left, uint8_t brake_right);

void set_status_player_number(uint8_t player);

void set_status_transport(dongle_transport_status_t status);

void get_status(dongle_status_s *out);

void set_session(dongle_session_s *session);

void get_session(dongle_session_s *session);

/* ------------------------------------------------------------------------- */
/* Wireless link state machine                                               */
/*                                                                           */
/* Single writer = core 1 (the RX/watchdog owner). Readers: core 1 (pump)    */
/* and core 0 (RGB). Backed by its own cross-core snapshot, independent of   */
/* the transmitted dongle_status_s.                                          */
/* ------------------------------------------------------------------------- */

/* Mark the link UP and stamp activity. Call on every gamepad packet whose
 * session matches the adopted session; this also refreshes the watchdog. */
void set_link_up(uint64_t now_us);

/* Force the link DOWN and reset the state-machine timers. */
void set_link_down(void);

/* Watchdog: if the link is UP and no matching packet has arrived within the
 * timeout, transition to DOWN. Returns true only on the UP->DOWN edge so the
 * caller can drain queues / prepare for reinit. */
bool link_check_timeout(uint64_t now_us);

/* Current link status (core-safe snapshot read). */
dongle_link_status_t get_link_status(void);

#ifdef __cplusplus
}
#endif

#endif