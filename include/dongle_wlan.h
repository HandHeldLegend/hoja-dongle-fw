#ifndef DONGLE_WLAN_H
#define DONGLE_WLAN_H

/*
 * Shared WLAN constants and core-0 status helpers used by transport/cores.
 * Entry points for each CPU are dongle_wlan_core0_poll() and dongle_wlan_core1_poll().
 */

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DONGLE_WLAN_UDP_PORT 4444
#define DONGLE_WLAN_QUEUE_LEN 16
#define DONGLE_WLAN_WAKE_INTERVAL_US 100000u
#define DONGLE_WLAN_PUMP_INTERVAL_US 2000u
#define DONGLE_WLAN_TIMEOUT_US 5000000u

dongle_status_u *dongle_current_status(void);
void dongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right);
void dongle_update_connection_status(dongle_connection_t connection);
void dongle_update_player_number(uint8_t player);

bool dongle_wlan_queue_output(const uint8_t *data, uint16_t len);
bool dongle_wlan_peek_reliable(uint8_t *data, uint16_t *len);
void dongle_wlan_consume_reliable(void);

#ifdef __cplusplus
}
#endif

#endif
