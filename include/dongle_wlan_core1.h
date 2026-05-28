#ifndef DONGLE_WLAN_CORE1_H
#define DONGLE_WLAN_CORE1_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    dongle_pkt_s pkt;
    ip_addr_t addr;
    u16_t port;
} dongle_wlan_rx_frame_t;

void dongle_wlan_core1_init(struct udp_pcb *pcb);
void dongle_wlan_core1_on_rx(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void dongle_wlan_core1_poll(uint64_t now_us);

bool dongle_wlan_core1_queue_output(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
