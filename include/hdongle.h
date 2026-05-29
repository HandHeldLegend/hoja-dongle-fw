#ifndef HDONGLE_H
#define HDONGLE_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HDONGLE_WLAN_UDP_PORT DONGLE_WLAN_PORT
#define HDONGLE_QUEUE_LEN 16
#define HDONGLE_WAKE_INTERVAL_US 100000u
#define HDONGLE_TIMEOUT_US 5000000u
/** hdongle_link_pump() no-ops if called sooner than this (~500 Hz max). */
#define HDONGLE_LINK_PUMP_MIN_INTERVAL_US 1800u

/* Core 0 — gamepad → host (cores choose unreliable and/or reliable) */
bool hdongle_rx_unreliable_read_core0(dongle_pkt_s *pkt);
bool hdongle_core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len);

/* Core 0 — host → gamepad */
void hdongle_core0_send_reliable_outputreport(const uint8_t *data, uint16_t len);

/**
 * Schedule WLAN STATUS/reliable TX at pump_at_us (core0 / transport / SOF).
 * Core1 sends when its clock reaches pump_at_us. Safe from ISR.
 * No-op if within HDONGLE_LINK_PUMP_MIN_INTERVAL_US of the last completed pump.
 * pump_at_us is clamped to not be in the past.
 */
void hdongle_link_pump(uint64_t pump_at_us);

/** Clear host-poll timing (link loss, transport stop, joybus reset). */
void hdongle_link_pump_reset_timing(void);

/**
 * Schedule the next WLAN pump halfway between the last host poll and now.
 * No-op until hdongle_link_pump_mark_sent() has been called at least once.
 * Safe from ISR (USB SOF, joybus).
 */
void hdongle_link_pump_schedule_from_poll(uint64_t now_us);

/** Record that the host received a poll response (USB report / joybus TX). */
void hdongle_link_pump_mark_sent(uint64_t now_us);

/* Status (core 0 writes, core 1 reads snapshot for STATUS TX) */
dongle_status_u *hdongle_current_status(void);
void hdongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right);
void hdongle_update_connection_status(dongle_connection_t connection);
void hdongle_update_player_number(uint8_t player);

/* Per-core main loops */
void hdongle_core0(uint64_t time_us);
void hdongle_core1(uint64_t time_us);

#ifdef __cplusplus
}
#endif

#endif
