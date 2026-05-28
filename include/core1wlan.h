/*
 * Core 1 WLAN public API (cross-core TX, entry point, link-pump scheduling).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core1wlan.h
 * @brief Public API for the core 1 WLAN radio/link module.
 *
 * Declares the cross-core entry used by core 0 to queue host->gamepad packets,
 * the core 1 thread entry point, and the link-pump scheduling hooks that let the
 * USB SOF / Joybus poll timing drive when core 1 transmits to the gamepad.
 */

#ifndef CORE1WLAN_H
#define CORE1WLAN_H

#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

void core1_pump_timer_mark_sent(void);
void core1_send_to_wlan_tx_mailbox(const dongle_pkt_s *pkt);
void core1_entry(void);

#ifdef __cplusplus
}
#endif

#endif
