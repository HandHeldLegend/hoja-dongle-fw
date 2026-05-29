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

void dongle_wlan_core0_signal_link_timeout(void);
void dongle_wlan_core0_reset(void);

void dongle_wlan_core0_status_snapshot_read(dongle_status_u *out);

#ifdef __cplusplus
}
#endif

#endif
