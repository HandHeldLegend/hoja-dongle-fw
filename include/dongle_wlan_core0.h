#ifndef DONGLE_WLAN_CORE0_H
#define DONGLE_WLAN_CORE0_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#ifdef __cplusplus
extern "C" {
#endif

void dongle_wlan_core0_init(void);
void dongle_wlan_core0_poll(uint64_t now_us);
void dongle_wlan_core0_set_boot_format(uint8_t format);

/* Called from core 1 when a gamepad packet should be handled on core 0. */
bool dongle_wlan_core0_inbox_push(const dongle_pkt_s *pkt);

/* Called from core 1 on link timeout / reset. */
void dongle_wlan_core0_signal_link_timeout(void);
void dongle_wlan_core0_reset(void);

void dongle_wlan_core0_status_snapshot_read(dongle_status_u *out);

bool dongle_wlan_core0_peek_reliable(uint8_t *data, uint16_t *len);
void dongle_wlan_core0_consume_reliable(void);

#ifdef __cplusplus
}
#endif

#endif
