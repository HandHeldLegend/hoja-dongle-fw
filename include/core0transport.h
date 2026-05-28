/*
 * Core 0 transport/protocol public API (cross-core inbox, input/output, status).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core0transport.h
 * @brief Public API for the core 0 transport/protocol engine.
 *
 * Declares the cross-core entry point used by core 1 to deliver received gamepad
 * packets, the input/output report accessors used by the active gamepad core, and
 * the status setters/getter. Core 0 is the single writer of the status snapshot
 * (@c transport_status, rumble, player). The wireless link is tracked separately
 * by the core 1-owned link state machine in central.c.
 */

#ifndef CORE0TRANSPORT_H
#define CORE0TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

void core0_send_to_gamepad_tx_mailbox(const dongle_pkt_s *pkt);

// Returns ONLY unreliable 
bool core0_get_unreliable_pkt(dongle_pkt_s *pkt);

// Returns one packet parsed out for its data, either reliable or unreliable
bool core0_get_inputreport(uint8_t *data, uint16_t *len);

void core0_send_reliable_outputreport(const uint8_t *data, uint16_t len);
void core0_send_unreliable_outputreport(const uint8_t *data, uint16_t len);

void core0_task(void);

#ifdef __cplusplus
}
#endif

#endif
