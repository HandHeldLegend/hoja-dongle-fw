#ifndef DONGLE_WLAN_H
#define DONGLE_WLAN_H

/*
 * Shared WLAN API (core 0 = cores/transport, core 1 = WiFi/UDP).
 *
 * Gamepad → host input:  dongle_wlan_read_next()
 * Host → gamepad OUT:    dongle_wlan_queue_output()
 * Session / core init:   dongle_wlan_wake_post() (core 1) → dongle_wlan_wake_consume() (core 0)
 *
 * See dongle_wlan.c for the wake mailbox design.
 */

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DONGLE_WLAN_UDP_PORT DONGLE_WLAN_PORT
#define DONGLE_WLAN_QUEUE_LEN 16
#define DONGLE_WLAN_WAKE_INTERVAL_US 100000u
#define DONGLE_WLAN_PUMP_INTERVAL_US 2000u
#define DONGLE_WLAN_TIMEOUT_US 5000000u

dongle_status_u *dongle_current_status(void);
void dongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right);
void dongle_update_connection_status(dongle_connection_t connection);
void dongle_update_player_number(uint8_t player);

bool dongle_wlan_queue_output(const uint8_t *data, uint16_t len);
bool dongle_wlan_read_next(uint8_t *data, uint16_t *len);

bool dongle_wlan_write_unreliable(const uint8_t *data, uint16_t len);
bool dongle_wlan_queue_reliable(const uint8_t *data, uint16_t len);

/*
 * WAKE mailbox (core 1 producer, core 0 consumer).
 *
 * Core 1 calls wake_post() when the gamepad sends a WAKE with dongle_wake_s payload.
 * Duplicate back-to-back WAKEs (same session + same wake body) are dropped on core 1
 * unless link_just_established is true (gamepad came back after an RX timeout).
 *
 * Core 0 calls wake_consume() once per poll; if it returns true, run core_init from
 * the returned wake/session. Each post is consumed at most once.
 */
bool dongle_wlan_wake_post(const dongle_pkt_s *pkt, bool link_just_established);
bool dongle_wlan_wake_consume(dongle_wake_s *wake, dongle_session_s *session);

void dongle_wlan_reset(void);

#ifdef __cplusplus
}
#endif

#endif
