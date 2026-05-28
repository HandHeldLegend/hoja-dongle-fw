/*
 * WLAN dongle radio + link state machine (Pico core 1).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core1wlan.c
 * @brief WLAN dongle radio + link state machine (Pico core 1).
 *
 * Core 1 owns the Wi-Fi AP, DHCP, the UDP socket, the WLAN link state machine,
 * WAKE beacons, the reliable host->gamepad ACK/resend lane, and host-paced TX.
 *
 * Cross-core contract (mirrors core0transport.c):
 *   RX  (core1 -> core0):  every accepted gamepad packet is forwarded with
 *                          core0_send_pkt(). Core 0 interprets WAKE/STATUS/etc.
 *   TX  (core0 -> core1):  core1_send_pkt() queues reliable OUT for this core to
 *                          transmit. Outbound STATUS reads rumble/transport via
 *                          get_status(). The wireless link lives in its own
 *                          state machine (central.c), owned/written by core 1.
 *
 * All cross-core objects here are strict single-producer / single-consumer.
 */

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include <dongle.h>
#include <dhcpserver.h>

#include "core0transport.h"
#include "core1wlan.h"
#include "central.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"

#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"

#include "utilities/crosscore_snapshot.h"
#include "utilities/crosscore_fifo.h"
#include "utilities/interval.h"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

#define C1_WLAN_UDP_PORT DONGLE_WLAN_PORT
#define C1_QUEUE_LEN 64
#define C1_WAKE_INTERVAL_US 1000000u
/* Link timeout/state now lives in central.c (link state machine). */
/* Link pump no-ops if called sooner than this (~500 Hz max). */
#define C1_LINK_PUMP_MIN_INTERVAL_US 1900u

/* ========================================================================== */
/* Cross-core FIFOs                                                           */
/* ========================================================================== */

/* lwIP RX callback (TCP/IP context) -> core1 loop. */
typedef struct
{
    dongle_pkt_s pkt;
    ip_addr_t addr;
    u16_t port;
} c1_udp_rx_frame_t;

CROSSCORE_FIFO_TYPE(c1_udp_rx, c1_udp_rx_frame_t, C1_QUEUE_LEN);
static fifo_c1_udp_rx_t _cc_udp_rx;

/* core0 (core1_send_pkt) -> core1 reliable OUT pump. */
CROSSCORE_FIFO_TYPE(c1_tx_reliable, dongle_pkt_s, C1_QUEUE_LEN);
static fifo_c1_tx_reliable_t _cc_tx_reliable;

/* core0 (core1_send_config) -> core1 config reliable OUT pump. */
CROSSCORE_FIFO_TYPE(c1_tx_config_reliable, dongle_pkt_s, C1_QUEUE_LEN);
static fifo_c1_tx_config_reliable_t _cc_tx_config_reliable;

static struct udp_pcb *_pcb;
static ip_addr_t _gamepad_addr; /* Fixed DONGLE_GAMEPAD_IP* from dongle.h */

/* ========================================================================== */
/* Status setting/getting                                                     */
/* ========================================================================== */

/* Non-zero while a reliable OUT packet is awaiting its ack echo from the
 * gamepad. Acks assigned by core 0 are always non-zero, so 0 means idle. */
static uint16_t _inflight_ack = 0;
static dongle_pkt_s _inflight_reliable_pkt;

/* Load the next queued reliable OUT packet into the inflight slot, but only
 * while the lane is idle so we never clobber a packet still awaiting its ack. */
static void _c1_arm_inflight_reliable(void)
{
    if(_inflight_ack != 0)
        return;

    dongle_pkt_s pkt;
    if(fifo_c1_tx_reliable_pop(&_cc_tx_reliable, &pkt))
    {
        _inflight_ack = pkt.ack;
        _inflight_reliable_pkt = pkt;
    }
}

static bool _c1_udp_send(const dongle_pkt_s *pkt, const ip_addr_t *addr, u16_t port)
{
    if (!_pcb || !addr)
    {
        return false;
    }

    struct pbuf *txp = pbuf_alloc(PBUF_TRANSPORT, sizeof(dongle_pkt_s), PBUF_RAM);
    if (!txp)
    {
        return false;
    }

    memcpy(txp->payload, pkt, sizeof(dongle_pkt_s));
    err_t err = udp_sendto(_pcb, txp, addr, port);
    pbuf_free(txp);
    return err == ERR_OK;
}

static bool _c1_send_to_gamepad(const dongle_pkt_s *pkt)
{
    return _c1_udp_send(pkt, &_gamepad_addr, DONGLE_GAMEPAD_PORT);
}

static bool _c1_send_reliable(void)
{
    /* Pull the next queued packet in if the lane is currently idle. */
    _c1_arm_inflight_reliable();

    /* Nothing awaiting ack: let the caller fall back to a STATUS packet. */
    if(_inflight_ack == 0)
        return false;

    /* Resend the inflight packet; the gamepad must echo _inflight_ack to
     * retire it (handled in _c1_rx_consume). */
    _c1_send_to_gamepad(&_inflight_reliable_pkt);
    return true;
}

static bool _c1_send_status(void)
{
    dongle_status_u status;
    get_status(&status);

    dongle_session_s session;
    get_session(&session);

    dongle_pkt_s pkt;

    pkt.session = dongle_session_pack(&session);
    pkt.ack = 0;
    pkt.id = DONGLE_PID_STATUS;
    
    memcpy(pkt.data, &status, sizeof(status));
    pkt.len = sizeof(status);

    return _c1_send_to_gamepad(&pkt);
}

static bool _c1_send_wake(void)
{
    dongle_pkt_s pkt;
    pkt.ack = 0;
    pkt.id  = DONGLE_PID_WAKE;
    pkt.len = 0;
    pkt.session = 0;

    return _c1_send_to_gamepad(&pkt);
}

/* ========================================================================== */
/* RX ingress                                                                 */
/* ========================================================================== */

/* Clear the reliable lane: drop the inflight packet and flush both OUT queues.
 * Their packets were stamped for the old session, so they must not survive a
 * link drop into a fresh session. Core 1 is the sole consumer of these FIFOs. */
static void _c1_link_reset(void)
{
    _inflight_ack = 0;

    dongle_pkt_s scratch;
    while (fifo_c1_tx_reliable_pop(&_cc_tx_reliable, &scratch)) { }
    while (fifo_c1_tx_config_reliable_pop(&_cc_tx_config_reliable, &scratch)) { }
}

/**
 * Drain UDP RX FIFO, then:
 *   - keep the link UP while the gamepad's session matches the adopted one,
 *   - echo any reliable ack,
 *   - forward the packet to core0 for protocol handling.
 */
static void _c1_rx_consume(void)
{
    c1_udp_rx_frame_t frame;

    /* Read all pending UDP messages from the gamepad */
    while (fifo_c1_udp_rx_pop(&_cc_udp_rx, &frame))
    {   
        /* Pointer to packet data for code cleanliness */
        const dongle_pkt_s *pkt = &frame.pkt;

        /* Raise the link immediately on the WAKE that brings the core up, then
         * keep it UP while the gamepad's session matches the adopted one. Either
         * case refreshes the watchdog. */
        dongle_session_s session;
        get_session(&session);
        if (pkt->id == DONGLE_PID_WAKE || pkt->session == dongle_session_pack(&session))
        {
            set_link_up(time_us_64());
        }

        /* Reliable lane: if the gamepad echoed the ack of the packet we have
         * inflight, retire it and tee up the next queued reliable packet. */
        if (_inflight_ack != 0 && pkt->ack == _inflight_ack)
        {
            _inflight_ack = 0;
            _c1_arm_inflight_reliable();
        }

        /* Forward packet to Core 0 */
        core0_send_to_gamepad_tx_mailbox(pkt);
    }
}

/* ========================================================================== */
/* Link pump scheduling (public; safe from core0 / ISR)                       */
/* ========================================================================== */

SNAPSHOT_TYPE(pump, uint64_t);

/* Scheduled pump deadline (absolute time_us_64). SINGLE WRITER = core 0 via
 * core1_pump_timer_mark_sent(); core 1 only reads it. */
static snapshot_pump_t _cc_next_pump;

/* Timestamp of our last pump. Touched only by core 1, so it is plain local
 * state rather than a cross-core snapshot. */
static uint64_t _last_pump_us = 0;

#define PUMP_TIME_MIN_US 1900u
#define PUMP_TIME_MAX_US 1000000u // 1 second max

bool _c1_pump_task(void)
{
    uint64_t now_us = time_us_64();
    bool pump_it = false;

    uint64_t next_pump;
    snapshot_pump_read(&_cc_next_pump, &next_pump);

    // Time since our last pump
    uint64_t delta = now_us - _last_pump_us;

    // A scheduled deadline newer than our last pump has arrived. Comparing the
    // deadline against _last_pump_us dedupes the schedule, so we never have to
    // write _cc_next_pump from core 1 (core 0 stays its sole writer).
    if (next_pump > _last_pump_us && now_us >= next_pump)
    {
        pump_it = true;
    }
    // Heartbeat fallback if we've gone too long without a pump.
    else if (delta >= PUMP_TIME_MAX_US)
    {
        pump_it = true;
    }

    if (pump_it)
    {
        _last_pump_us = now_us;
        return true;
    }

    return false;
}

// ISR context, we push the whole frame struct
// into a FIFO for processing in the main thread
// outside of ISR context :)
static void _c1_udp_rx_cb(void *arg, struct udp_pcb *udp, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)udp;

    if (!p || p->tot_len != sizeof(dongle_pkt_s) || !addr)
    {
        if (p)
        {
            pbuf_free(p);
        }
        return;
    }

    c1_udp_rx_frame_t frame;
    pbuf_copy_partial(p, &frame.pkt, sizeof(dongle_pkt_s), 0);
    ip_addr_copy(frame.addr, *addr);
    frame.port = port;
    fifo_c1_udp_rx_push(&_cc_udp_rx, &frame);
    pbuf_free(p);
}

/* ========================================================================== */
/* Init + main loop                                                           */
/* ========================================================================== */

static void _c1_task(void)
{
    // Drain inbound UDP first: forwards packets to core 0 and retires the
    // inflight reliable packet when the gamepad echoes its ack.
    _c1_rx_consume();

    // Watchdog: drop the link after the timeout with no matching packet. On the
    // UP->DOWN edge, flush the reliable lane so stale packets don't reach a new
    // session once we reconnect.
    if(link_check_timeout(time_us_64()))
    {
        _c1_link_reset();
    }

    // Should we send a packet?
    if(_c1_pump_task())
    {
        // No wake needed
        if(get_link_status() == DONGLE_LINK_UP)
        {
            if(!_c1_send_reliable())
            {
                _c1_send_status();
            }
        }
        else
        {
            // Send wake
            _c1_send_wake();
        }
    }
    
}

/* ========================================================================== */
/* Public Functions                                                           */
/* ========================================================================== */

// Call at the boundaries of when a packet is sent
// to allow our pump timer to automatically send
// a status update at an appropriate interval
void core1_pump_timer_mark_sent(void)
{
    static uint64_t last_time = 0;
    uint64_t this_time = time_us_64();

    uint64_t delta = this_time - last_time;
    uint64_t next_time = 0;

    // Skip if our delta is too small (time between polls)
    if (delta <= PUMP_TIME_MIN_US)
    {
        return;
    }
    else
    {
        last_time = this_time;
        next_time = this_time + (delta >> 1);
        snapshot_pump_write(&_cc_next_pump, &next_time);
    }
}

void core1_send_to_wlan_tx_mailbox(const dongle_pkt_s *pkt)
{
    if (!pkt)
    {
        return;
    }

    switch (pkt->id)
    {
    case DONGLE_PID_CONFIG_RELIABLE:
        fifo_c1_tx_config_reliable_push(&_cc_tx_config_reliable, pkt);
        break;

    case DONGLE_PID_CORE_RELIABLE:
        /* Core1 pump will pop, assign ack, and resend until gamepad echoes it. */
        fifo_c1_tx_reliable_push(&_cc_tx_reliable, pkt);
        break;

    default:
        break;
    }
}

void core1_entry(void)
{
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
    {
        return;
    }

    cyw43_wifi_ap_set_channel(&cyw43_state, 6);
    cyw43_arch_enable_ap_mode(WIFI_SSID_BASE, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    cyw43_wifi_set_roam_enabled(&cyw43_state, false);
    cyw43_wifi_set_interference_mode(&cyw43_state, CYW43_IFMODE_NONE);

    static dhcp_server_t dhcp_server;
    ip_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_netmask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    struct udp_pcb *udp = udp_new();
    udp_bind(udp, IP_ANY_TYPE, C1_WLAN_UDP_PORT);
    udp_recv(udp, _c1_udp_rx_cb, NULL);

    _pcb = udp;
    IP4_ADDR(&_gamepad_addr, DONGLE_GAMEPAD_IP0, DONGLE_GAMEPAD_IP1, DONGLE_GAMEPAD_IP2, DONGLE_GAMEPAD_IP3);

    for (;;)
    {
        _c1_task();
    }
}

