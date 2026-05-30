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
 * the status setters/getter. Core 0 is the single writer of the status snapshot;
 * the wireless @c link_status is merged in from core 1, while @c transport_status
 * (USB / Joybus) is owned by core 0.
 */

#ifndef CORE0TRANSPORT_H
#define CORE0TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hand a received gamepad packet from core 1 to core 0 for processing.
 *
 * Core 1 is the ONLY producer (SPSC). Unreliable input is stored as a
 * latest-value snapshot; bulk input updates the bulk snapshot; everything else is
 * queued into the core 0 mailbox and decoded in core0's task loop.
 *
 * @param pkt Packet received from the gamepad (copied; caller retains ownership).
 */
void core0_send_pkt(const dongle_pkt_s *pkt);

/**
 * @brief Fetch the latest input report for the active gamepad core.
 *
 * Prefers a queued reliable IN report; otherwise falls back to the latest
 * unreliable snapshot.
 *
 * @param[out] data Destination buffer for the report payload.
 * @param[out] len  Receives the payload length on success.
 * @return true if a report was copied, false if none is available.
 */
bool core0_get_inputreport(uint8_t *data, uint16_t *len);

/**
 * @brief Read the latest CORE_UNRELIABLE input snapshot.
 * @param[out] pkt Destination packet; @c pkt->len > 0 indicates valid data.
 * @return true if a non-empty snapshot was copied.
 */
bool core0_get_unreliable_inputreport(dongle_pkt_s *pkt);

/**
 * @brief Pop one queued CORE_RELIABLE IN report (gamepad -> host).
 * @param[out] data Destination buffer for the report payload.
 * @param[out] len  Receives the payload length on success.
 * @return true if a queued report was dequeued, false if the queue was empty.
 */
bool core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len);

/**
 * @brief Queue a reliable host -> gamepad OUT report for core 1 to transmit.
 * @param data Payload to send (max 64 bytes; longer/empty inputs are dropped).
 * @param len  Payload length in bytes.
 */
void core0_send_reliable_outputreport(const uint8_t *data, uint16_t len);

/**
 * @brief Update rumble/brake actuator levels in the status snapshot.
 * @param left        Primary (left) rumble amplitude.
 * @param right       Primary (right) rumble amplitude.
 * @param brake_left  Left brake/secondary amplitude.
 * @param brake_right Right brake/secondary amplitude.
 */
void core0_set_rumble(uint8_t left, uint8_t right, uint8_t brake_left, uint8_t brake_right);

/**
 * @brief Set the assigned player number in the status snapshot.
 * @param player Player index reported to the gamepad.
 */
void core0_set_player_number(uint8_t player);

/**
 * @brief Set the dongle->console transport state (USB active / Joybus connected).
 *
 * Core 0 owned. No-ops if the value is unchanged.
 *
 * @param status DONGLE_TRANSPORT_CONNECTED or DONGLE_TRANSPORT_IDLE.
 */
void core0_set_transport_status(dongle_transport_status_t status);

/**
 * @brief Read the current status snapshot.
 *
 * Used by core 1 to build outbound STATUS packets; it stamps the wireless
 * @c link_status itself.
 *
 * @param[out] out Destination status union.
 */
void core0_get_status(dongle_status_u *out);

#ifdef __cplusplus
}
#endif

#endif
