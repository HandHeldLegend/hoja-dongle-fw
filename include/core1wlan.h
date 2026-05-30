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

/**
 * @brief Queue a packet from core 0 for transmission to the gamepad.
 *
 * Used for reliable host->gamepad OUT (and config) reports. Core 0 is the ONLY
 * producer (SPSC). The packet is routed to the appropriate core 1 TX lane by its
 * @c id; other ids are ignored.
 *
 * @param pkt Packet to transmit (copied; caller retains ownership).
 */
void core1_send_pkt(const dongle_pkt_s *pkt);

/**
 * @brief Core 1 entry point. Pass to multicore_launch_core1(); never returns.
 *
 * Brings up the Wi-Fi AP, DHCP server and UDP socket, then runs the WLAN link
 * state machine and TX scheduler forever.
 */
void core1_entry(void);

/**
 * @brief Request a link-pump transmit at (or after) the given deadline.
 *
 * Safe to call from core 0, the USB SOF callback, or a Joybus ISR. Rate-limited
 * internally; core 1 emits one STATUS or reliable-OUT packet when its clock
 * reaches the scheduled time.
 *
 * @param pump_at_us Desired transmit deadline (microseconds, time_us_64 base).
 */
void core1_link_pump(uint64_t pump_at_us);

/** @brief Reset poll-derived pump timing (e.g. on transport start/stop or link loss). */
void core1_link_pump_reset_timing(void);

/**
 * @brief Schedule the next pump from the host poll cadence.
 *
 * Derives a mid-interval transmit deadline from the spacing between host polls so
 * the gamepad is serviced between polls.
 *
 * @param now_us Current time (microseconds, time_us_64 base).
 */
void core1_link_pump_schedule_from_poll(uint64_t now_us);

/**
 * @brief Record the time of the most recent host poll response.
 * @param now_us Current time (microseconds, time_us_64 base).
 */
void core1_link_pump_mark_sent(uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif
