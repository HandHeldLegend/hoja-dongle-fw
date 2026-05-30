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
 *                          core0_get_status(); the wireless link_status is stamped from the SM.
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

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"

#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"

#include "utilities/crosscore_fifo.h"
#include "utilities/interval.h"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

#define C1_WLAN_UDP_PORT DONGLE_WLAN_PORT
#define C1_QUEUE_LEN 16
#define C1_WAKE_INTERVAL_US 100000u
#define C1_TIMEOUT_US 5000000u
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

/* ========================================================================== */
/* WLAN state machine                                                         */
/* ========================================================================== */

typedef enum
{
    C1_LINK_DOWN, /* No recent gamepad RX — WAKE beacons only */
    C1_LINK_UP,   /* Gamepad heard — pump via core1_link_pump() only */
} c1_link_t;

typedef enum
{
    C1_RELIABLE_IDLE,      /* No host OUT waiting for gamepad ack */
    C1_RELIABLE_AWAIT_ACK, /* CORE_RELIABLE on wire; pkt.ack must match expected_ack */
} c1_reliable_t;

typedef struct
{
    c1_link_t link;
    uint64_t last_rx_us;      /* Last valid UDP RX from gamepad (for timeout) */
    dongle_session_s session; /* Stamped into outbound dongle_pkt_s.session */

    c1_reliable_t reliable;
    uint16_t prev_ack;     /* Last ack token sent (never reuse back-to-back) */
    uint16_t expected_ack; /* While AWAIT_ACK: gamepad must echo this in pkt->ack */
    dongle_pkt_s inflight; /* Host OUT payload being acknowledged */

    interval_s wake_iv; /* WAKE beacon timer while LINK_DOWN */
} c1_wlan_sm_t;

static struct udp_pcb *_pcb;
static ip_addr_t _gamepad_addr; /* Fixed DONGLE_GAMEPAD_IP* from dongle.h */
static c1_wlan_sm_t _sm;

/* Link pump scheduling (atomics shared with the calling core / ISR). */
static atomic_ullong _link_pump_at_us;        /* Next pump deadline (0 = none) */
static atomic_ullong _link_pump_last_done_us; /* Last completed pump (rate limit) */
static atomic_ullong _link_poll_last_sent_us; /* Last host poll response time */

/* ========================================================================== */
/* Session / packet helpers                                                   */
/* ========================================================================== */

static uint16_t _c1_session_pack(const dongle_session_s *s)
{
    uint16_t v = 0;
    memcpy(&v, s, sizeof(uint16_t));
    return v;
}

static void _c1_session_unpack(uint16_t packed, dongle_session_s *out)
{
    memcpy(out, &packed, sizeof(uint16_t));
}

/** Build outbound dongle_pkt_s using current SM session and supplied ack/payload. */
static void _c1_build_pkt(const c1_wlan_sm_t *sm, dongle_pkt_s *pkt, dongle_pid_t pid, uint16_t ack,
                          const uint8_t *data, uint16_t len)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->session = _c1_session_pack(&sm->session);
    pkt->ack = ack;
    pkt->id = (uint8_t)pid;
    pkt->len = len;
    if (data && len > 0)
    {
        uint16_t n = len > sizeof(pkt->data) ? (uint16_t)sizeof(pkt->data) : len;
        memcpy(pkt->data, data, n);
        pkt->len = n;
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

static bool _c1_gamepad_addr_matches(const ip_addr_t *addr, u16_t port)
{
    return port == DONGLE_GAMEPAD_PORT && ip_addr_cmp(addr, &_gamepad_addr);
}

/* ========================================================================== */
/* Connection reporting (core1 -> core0)                                      */
/* ========================================================================== */

/** Tell core0 the current wireless link state so it can publish it in the status snapshot. */
static void _c1_report_link(const c1_wlan_sm_t *sm)
{
    dongle_status_u st = {0};
    st.link_status = (sm->link == C1_LINK_UP) ? DONGLE_LINK_UP : DONGLE_LINK_DOWN;

    dongle_pkt_s pkt = {0};
    pkt.id = DONGLE_PID_STATUS;
    pkt.len = sizeof(dongle_status_u);
    memcpy(pkt.data, &st, sizeof(dongle_status_u));
    core0_send_pkt(&pkt);
}

/* ========================================================================== */
/* Reliable host->gamepad lane                                                */
/* ========================================================================== */

static bool _c1_reliable_awaiting_ack(const c1_wlan_sm_t *sm)
{
    return sm->reliable == C1_RELIABLE_AWAIT_ACK;
}

static void _c1_reliable_clear_inflight(c1_wlan_sm_t *sm)
{
    sm->reliable = C1_RELIABLE_IDLE;
    sm->expected_ack = 0;
    memset(&sm->inflight, 0, sizeof(sm->inflight));
}

/** Pick a random 16-bit ack token distinct from prev_ack; store as expected_ack. */
static uint16_t _c1_reliable_assign_ack(c1_wlan_sm_t *sm)
{
    uint16_t ack;
    do
    {
        ack = (uint16_t)(get_rand_32() & 0xFFFFu);
    } while (ack == sm->prev_ack);
    sm->prev_ack = ack;
    sm->expected_ack = ack;
    return ack;
}

/** If lane is free, pop next host OUT from the TX FIFO and go AWAIT_ACK. */
static bool _c1_reliable_try_dequeue(c1_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;

    if (_c1_reliable_awaiting_ack(sm))
    {
        return false;
    }

    if (!fifo_c1_tx_reliable_pop(&_cc_tx_reliable, &pkt))
    {
        return false;
    }

    sm->inflight = pkt;
    _c1_reliable_assign_ack(sm);
    sm->reliable = C1_RELIABLE_AWAIT_ACK;
    return true;
}

/** Gamepad echoes our ack; match retires inflight and may start the next OUT. */
static void _c1_reliable_on_ack(c1_wlan_sm_t *sm, uint16_t echoed_ack)
{
    // Only proceed if we are awaiting a reliable ack or if our ack matches
    if (!_c1_reliable_awaiting_ack(sm) || echoed_ack != sm->expected_ack)
    {
        return;
    }

    _c1_reliable_clear_inflight(sm);
    _c1_reliable_try_dequeue(sm);
}

static void _c1_reliable_reset(c1_wlan_sm_t *sm)
{
    sm->prev_ack = 0;
    _c1_reliable_clear_inflight(sm);

    dongle_pkt_s drop;
    while (fifo_c1_tx_reliable_pop(&_cc_tx_reliable, &drop))
    {
    }
}

/* ========================================================================== */
/* Link lifecycle                                                             */
/* ========================================================================== */

static void _c1_sm_init(c1_wlan_sm_t *sm)
{
    memset(sm, 0, sizeof(*sm));
    sm->link = C1_LINK_DOWN;
    sm->reliable = C1_RELIABLE_IDLE;
    _c1_report_link(sm);
}

/** Promote LINK_DOWN -> LINK_UP on first RX; refresh watchdog timestamp. */
static void _c1_link_on_rx(c1_wlan_sm_t *sm, uint64_t now_us)
{
    sm->last_rx_us = now_us;
    if (sm->link == C1_LINK_DOWN)
    {
        sm->link = C1_LINK_UP;
        _c1_report_link(sm);
    }
}

static bool _c1_link_timed_out(const c1_wlan_sm_t *sm, uint64_t now_us)
{
    return sm->link == C1_LINK_UP && sm->last_rx_us > 0 &&
           (now_us - sm->last_rx_us) > C1_TIMEOUT_US;
}

/** Full link teardown after timeout: reliable lane, session, notify core0. */
static void _c1_reset_link(c1_wlan_sm_t *sm)
{
    _c1_reliable_reset(sm);
    sm->link = C1_LINK_DOWN;
    sm->last_rx_us = 0;
    memset(&sm->session, 0, sizeof(sm->session));
    core1_link_pump_reset_timing();
    atomic_store_explicit(&_link_pump_at_us, 0, memory_order_relaxed);
    _c1_report_link(sm);
}

/* ========================================================================== */
/* RX ingress                                                                 */
/* ========================================================================== */

/**
 * Drain UDP RX FIFO, validate source, then:
 *   - update link/watchdog,
 *   - echo any reliable ack,
 *   - stamp the current session for our outbound packets,
 *   - forward the packet to core0 for protocol handling.
 */
static void _c1_rx_consume(c1_wlan_sm_t *sm)
{
    c1_udp_rx_frame_t frame;

    /* Read UDP message */
    while (fifo_c1_udp_rx_pop(&_cc_udp_rx, &frame))
    {
        /* Port and Address must match expected values */
        if (!_c1_gamepad_addr_matches(&frame.addr, frame.port))
        {
            continue;
        }
        
        /* Pointer to packet data for code cleanliness */
        const dongle_pkt_s *pkt = &frame.pkt;

        /* Update link and watchdog for link timeout */
        _c1_link_on_rx(sm, time_us_64());

        /* Every gamepad packet can acknowledge our outstanding reliable OUT. */
        _c1_reliable_on_ack(sm, pkt->ack);

        /* Keep our outbound session id current (gamepad owns the session). */
        _c1_session_unpack(pkt->session, &sm->session);

        /* Empty payload except WAKE is ignored (keepalive / ack-only frames). */
        if (pkt->len == 0 && pkt->id != DONGLE_PID_WAKE)
        {
            continue;
        }

        /* Forward packet to Core 0 */
        core0_send_pkt(pkt);
    }
}

/* ========================================================================== */
/* TX                                                                         */
/* ========================================================================== */

/** Empty WAKE beacon to the fixed gamepad IP while link is down. */
static bool _c1_send_wake_beacon(const c1_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;
    _c1_build_pkt(sm, &pkt, DONGLE_PID_WAKE, 0, NULL, 0);
    return _c1_send_to_gamepad(&pkt);
}

/** STATUS to gamepad: rumble/player/transport from core0, wireless link from the SM. */
static bool _c1_send_status(const c1_wlan_sm_t *sm)
{
    dongle_status_u st;
    core0_get_status(&st);
    st.link_status = (sm->link == C1_LINK_UP) ? DONGLE_LINK_UP : DONGLE_LINK_DOWN;

    dongle_pkt_s pkt;
    _c1_build_pkt(sm, &pkt, DONGLE_PID_STATUS, 0, (const uint8_t *)&st, sizeof(dongle_status_u));
    return _c1_send_to_gamepad(&pkt);
}

/** Resend current inflight CORE_RELIABLE with expected_ack until gamepad echoes it. */
static bool _c1_send_inflight_reliable(const c1_wlan_sm_t *sm)
{
    if (!_c1_reliable_awaiting_ack(sm))
    {
        return false;
    }

    dongle_pkt_s pkt;
    _c1_build_pkt(sm, &pkt, DONGLE_PID_CORE_RELIABLE, sm->expected_ack, sm->inflight.data, sm->inflight.len);
    return _c1_send_to_gamepad(&pkt);
}

/**
 * One pump tick for reliable outgoing traffic.
 * Attempts to dequeue a new reliable packet if none is currently inflight,
 * then resends the inflight reliable packet if one exists.
 *
 * @param sm Pointer to the wireless state machine
 * @return true if a reliable packet was sent; false if no packet was available
 */
static bool _c1_pump_reliable(c1_wlan_sm_t *sm)
{
    // If no reliable packet is currently awaiting acknowledgment, try to dequeue a new one
    if (!_c1_reliable_awaiting_ack(sm))
    {
        _c1_reliable_try_dequeue(sm);
    }

    // If still no reliable packet inflight, nothing to send
    if (!_c1_reliable_awaiting_ack(sm))
    {
        return false;
    }

    // Resend the inflight reliable packet
    return _c1_send_inflight_reliable(sm);
}

/** One link pump: reliable resend if inflight, else STATUS. No-op while LINK_DOWN. */
static void _c1_pump_link(c1_wlan_sm_t *sm)
{
    if (sm->link != C1_LINK_UP)
    {
        return;
    }

    if (!_c1_pump_reliable(sm))
    {
        _c1_send_status(sm);
    }
}

/* ========================================================================== */
/* Link pump scheduling (public; safe from core0 / ISR)                       */
/* ========================================================================== */

void core1_link_pump(uint64_t pump_at_us)
{
    uint64_t now_us = time_us_64();
    uint64_t last_us = atomic_load_explicit(&_link_pump_last_done_us, memory_order_relaxed);

    if (last_us > 0 && (now_us - last_us) < C1_LINK_PUMP_MIN_INTERVAL_US)
    {
        return;
    }

    if (pump_at_us < now_us)
    {
        pump_at_us = now_us;
    }

    atomic_store_explicit(&_link_pump_at_us, pump_at_us, memory_order_release);
}

void core1_link_pump_reset_timing(void)
{
    atomic_store_explicit(&_link_poll_last_sent_us, 0, memory_order_relaxed);
}

void core1_link_pump_schedule_from_poll(uint64_t now_us)
{
    uint64_t last_sent_us = atomic_load_explicit(&_link_poll_last_sent_us, memory_order_relaxed);

    if (last_sent_us > 0)
    {
        core1_link_pump(now_us + ((now_us - last_sent_us) >> 1));
    }
}

void core1_link_pump_mark_sent(uint64_t now_us)
{
    atomic_store_explicit(&_link_poll_last_sent_us, now_us, memory_order_relaxed);
}

/**
 * Attempts to execute the link pump if scheduled time has arrived.
 * 
 * Checks if a link pump has been scheduled (via core1_link_pump) and if the
 * current time has reached or passed the scheduled time. Additionally enforces
 * a minimum interval between consecutive pump executions to prevent flooding.
 * If all conditions are met, executes the link pump and updates timing state.
 * 
 * @param sm       Pointer to the WLAN state machine
 * @param now_us   Current time in microseconds
 */
static void _c1_try_link_pump(c1_wlan_sm_t *sm, uint64_t now_us)
{
    // Load the scheduled pump time; if 0 or not yet reached, skip execution
    uint64_t at_us = atomic_load_explicit(&_link_pump_at_us, memory_order_acquire);
    if (at_us == 0 || now_us < at_us)
    {
        return;
    }

    // Check minimum interval since last pump execution to prevent flooding
    uint64_t last_us = atomic_load_explicit(&_link_pump_last_done_us, memory_order_relaxed);
    if (last_us > 0 && (now_us - last_us) < C1_LINK_PUMP_MIN_INTERVAL_US)
    {
        return;
    }

    // Clear the scheduled time and record this execution time
    atomic_store_explicit(&_link_pump_at_us, 0, memory_order_release);
    atomic_store_explicit(&_link_pump_last_done_us, now_us, memory_order_relaxed);
    
    // Execute the link pump
    _c1_pump_link(sm);
}

/**
 * Core1 periodic TX scheduler:
 *   - RX timeout -> full link reset + notify core0
 *   - LINK_DOWN  -> 100 ms WAKE beacons
 *   (LINK_UP TX is driven only by core1_link_pump scheduling.)
 */
static void _c1_pump_tx(c1_wlan_sm_t *sm, uint64_t now_us)
{
    if (_c1_link_timed_out(sm, now_us))
    {
        _c1_reset_link(sm);
        return;
    }

    if (sm->link == C1_LINK_DOWN)
    {
        if (interval_run(now_us, C1_WAKE_INTERVAL_US, &sm->wake_iv))
        {
            _c1_send_wake_beacon(sm);
        }
    }
}

/* ========================================================================== */
/* Core 0 -> Core 1 entry                                                     */
/* ========================================================================== */

void core1_send_pkt(const dongle_pkt_s *pkt)
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

/* ========================================================================== */
/* lwIP RX callback                                                           */
/* ========================================================================== */

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

static void _c1_task(uint64_t time_us)
{
    _c1_rx_consume(&_sm);
    _c1_try_link_pump(&_sm, time_us);
    _c1_pump_tx(&_sm, time_us);
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
    _c1_sm_init(&_sm);

    for (;;)
    {
        _c1_task(time_us_64());
    }
}
