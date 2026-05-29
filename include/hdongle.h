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
#define HDONGLE_PUMP_INTERVAL_US 2000u
#define HDONGLE_TIMEOUT_US 5000000u

/* Core 0 — gamepad → host (cores choose unreliable and/or reliable) */
bool hdongle_rx_unreliable_read_core0(dongle_pkt_s *pkt);
bool hdongle_core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len);

/* Core 0 — host → gamepad */
void hdongle_core0_send_reliable_outputreport(const uint8_t *data, uint16_t len);

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
