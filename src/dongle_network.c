/*
 * WLAN platform adapter for the Pico 2 W: CYW43 AP + DHCP + UDP transport.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file dongle_network.c
 * @brief Platform implementation of the dongle host wlan hooks (core 1).
 *
 * The hoja_lib_dongle host owns the protocol/state machine; this file owns the
 * radio. It implements the three wlan hooks the library calls out to:
 *
 *   - dongle_api_host_wlan_hook_ap_bringup : start the CYW43 AP, DHCP, and the
 *                                            UDP socket the protocol rides on.
 *   - dongle_api_host_wlan_hook_udp_tx     : send one datagram to the gamepad.
 *   - dongle_api_host_wlan_hook_reset_network : (optional) reset network state.
 *
 * Inbound datagrams are filtered for size and handed to the library via
 * dongle_api_host_wlan_udp_rx(). Everything here runs on core 1 (the wlan core),
 * matching the library's single-producer/single-consumer cross-core contract.
 */

#include <stdint.h>
#include <string.h>

#include <dongle.h>
#include <dongle_host.h>

#include "dongle_network.h"
#include "dhcpserver.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"

/* The UDP socket the protocol rides on (created during AP bring-up, core 1). */
static struct udp_pcb *_pcb = NULL;

/* lwIP RX callback: size-filter the datagram and feed it to the library. */
static void _udp_rx_cb(void *arg, struct udp_pcb *udp, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)udp;
    (void)port;

    if (!p || p->tot_len != sizeof(dongle_pkt_s) || !addr)
    {
        if (p)
        {
            pbuf_free(p);
        }
        return;
    }

    dongle_pkt_s pkt;
    pbuf_copy_partial(p, &pkt, sizeof(dongle_pkt_s), 0);

    dongle_api_host_wlan_udp_rx(&pkt);

    pbuf_free(p);
}

/* -------------------------------------------------------------------------- */
/* HOST WLAN HOOKS (strong platform implementations)                          */
/* -------------------------------------------------------------------------- */

bool dongle_api_host_wlan_hook_ap_bringup(const char *ssid, const char *password,
                                          uint8_t ip[4], uint8_t mask[4])
{
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
    {
        return false;
    }

    cyw43_wifi_ap_set_channel(&cyw43_state, 6);
    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    cyw43_wifi_set_roam_enabled(&cyw43_state, false);
    cyw43_wifi_set_interference_mode(&cyw43_state, CYW43_IFMODE_NONE);

    static dhcp_server_t dhcp_server;
    ip_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&ap_netmask, mask[0], mask[1], mask[2], mask[3]);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    _pcb = udp_new();
    if (!_pcb)
    {
        return false;
    }

    udp_bind(_pcb, IP_ANY_TYPE, DONGLE_WLAN_PORT);
    udp_recv(_pcb, _udp_rx_cb, NULL);

    return true;
}

void dongle_api_host_wlan_hook_udp_tx(const dongle_pkt_s *pkt, uint8_t ip[4], uint16_t port)
{
    if (!_pcb || !pkt)
    {
        return;
    }

    struct pbuf *txp = pbuf_alloc(PBUF_TRANSPORT, sizeof(dongle_pkt_s), PBUF_RAM);
    if (!txp)
    {
        return;
    }

    memcpy(txp->payload, pkt, sizeof(dongle_pkt_s));

    ip_addr_t addr;
    IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
    udp_sendto(_pcb, txp, &addr, port);

    pbuf_free(txp);
}

/* -------------------------------------------------------------------------- */
/* Core 1 entry                                                               */
/* -------------------------------------------------------------------------- */

void dongle_network_core1_entry(void)
{
    /* Pin reserved; the gamepad pairs on the default SSID/password. */
    dongle_cfg_host_s cfg = {0};

    /* Brings the AP up via dongle_api_host_wlan_hook_ap_bringup() above. */
    dongle_api_host_wlan_init(&cfg);

    for (;;)
    {
        dongle_api_host_wlan_task();
    }
}
