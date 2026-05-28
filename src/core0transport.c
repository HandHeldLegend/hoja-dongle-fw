/*
 * Core 0 transport/protocol engine: gamepad-core dispatch, status snapshot, cross-core inbox.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core0transport.c
 * @brief Core 0 protocol/transport engine for the HOJA WLAN dongle.
 *
 * Core 0 owns the dongle->console transport (USB / Joybus cores), the
 * authoritative status snapshot, and the inbox of packets handed up from core 1's
 * WLAN engine. It decodes WAKE / STATUS / reliable / unreliable packets, exposes
 * input reports to the active gamepad core, and publishes outbound status
 * (rumble / player / transport) that core 1 reads when building STATUS packets.
 *
 * Cross-core contract: every shared FIFO/snapshot here is strict
 * single-producer / single-consumer. Core 1 is the only producer into the inbox;
 * core 0 is the only writer of the status snapshot.
 */

#include <stdint.h>
#include <string.h>
#include <dongle.h>

#include "central.h"
#include "core0transport.h"
#include "core1wlan.h"

#include "utilities/rgb.h"
#include "utilities/crosscore_snapshot.h"
#include "utilities/crosscore_fifo.h"

#include "cores/cores.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"

#include "hardware/watchdog.h"

#define CORE0_MAILBOX_LEN 64

// Core 0 packet inbox from Core 1
CROSSCORE_FIFO_TYPE(core0_mailbox, dongle_pkt_s, CORE0_MAILBOX_LEN);
static fifo_core0_mailbox_t _cc_core0_mailbox;

// Core 0 packet storage for unreliable data from core 1 (Input data)
SNAPSHOT_TYPE(core0_unreliable, dongle_pkt_s);
static snapshot_core0_unreliable_t _ss_core0_unreliable;

CROSSCORE_FIFO_TYPE(core0_reliable, dongle_pkt_s, CORE0_MAILBOX_LEN);
static fifo_core0_reliable_t _cc_core0_reliable;

CROSSCORE_FIFO_TYPE(core0_config, dongle_pkt_s, CORE0_MAILBOX_LEN);
static fifo_core0_config_t _cc_core0_config;

SNAPSHOT_TYPE(core0_bulk, dongle_pkt_s);
static snapshot_core0_bulk_t _ss_core0_bulk;

// Send a data
// packet from core 1 to be processed in core 0
void core0_send_to_gamepad_tx_mailbox(const dongle_pkt_s *pkt)
{
    switch(pkt->id)
    {
        case DONGLE_PID_CORE_UNRELIABLE:
        snapshot_core0_unreliable_write(&_ss_core0_unreliable, pkt);
        break;

        case DONGLE_PID_BULK_UNRELIABLE:
        snapshot_core0_bulk_write(&_ss_core0_bulk, pkt);

        // All other packets go into
        // our mailbox queue
        default:
        fifo_core0_mailbox_push(&_cc_core0_mailbox, pkt);
        break;
    }
}

bool core0_get_reliable_config(uint8_t *data, uint16_t *len)
{
    dongle_pkt_s pkt;
    if(fifo_core0_config_pop(&_cc_core0_config, &pkt))
    {
        memcpy(data, pkt.data, pkt.len);
        *len = pkt.len;
        return true;
    }
}

// Called by gamepad cores
// to obtain report data
bool core0_get_inputreport(uint8_t *data, uint16_t *len)
{
    dongle_pkt_s pkt;
    if(fifo_core0_reliable_pop(&_cc_core0_reliable, &pkt))
    {
        memcpy(data, pkt.data, pkt.len);
        *len = pkt.len;
        return true;
    }

    snapshot_core0_unreliable_read(&_ss_core0_unreliable, &pkt);
    if(pkt.len)
    {
        memcpy(data, pkt.data, pkt.len);
        *len = pkt.len;
        return true;
    }

    return false;
}

// Latest CORE_UNRELIABLE snapshot (pkt.len>0 when valid)
bool core0_get_unreliable_pkt(dongle_pkt_s *pkt)
{
    if(!pkt) return false;
    snapshot_core0_unreliable_read(&_ss_core0_unreliable, pkt);
    return pkt->len > 0;
}

static uint16_t _c0_generate_ack(void)
{
    static uint16_t last_ack = 0;
    uint16_t ack = get_rand_32() & 0xFFFF;
    if(ack==last_ack)ack+=1;
    if(!ack) ack += 1;
    last_ack = ack;
    return ack;
}

// Queue a reliable host -> gamepad OUT report for core 1 to transmit
void core0_send_reliable_outputreport(const uint8_t *data, uint16_t len)
{
    if(!data || len == 0 || len > 64) return;

    dongle_pkt_s pkt;
    dongle_session_s session;
    get_session(&session);

    pkt.ack = _c0_generate_ack();
    pkt.session = dongle_session_pack(&session);
    pkt.id = DONGLE_PID_CORE_RELIABLE;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    core1_send_to_wlan_tx_mailbox(&pkt);
}

// Queue a reliable host -> gamepad OUT bulk report for core 1 to transmit
void core0_send_reliable_configreport(const uint8_t *data, uint16_t len)
{
    if(!data || len == 0 || len > 64) return;

    dongle_pkt_s pkt = {0};
    pkt.id = DONGLE_PID_CONFIG_RELIABLE;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    core1_send_to_wlan_tx_mailbox(&pkt);
}

static void _c0_process_wake(dongle_pkt_s *pkt)
{
    if(pkt->len != sizeof(dongle_wake_s)) return;

    dongle_wake_s tmp;
    memcpy(&tmp, pkt->data, pkt->len);

    dongle_session_s session;
    get_session(&session);
    uint16_t session_val = dongle_session_pack(&session);

    if(tmp.session != session_val)
    {
        // Deinit core (only applies if it's running too)
        core_deinit();

        sleep_ms(100);

        // core_init applies the matching USB mode + sets the mode LED.
        core_init(&tmp);

        // Set session
        dongle_session_unpack(tmp.session, &session);
        set_session(&session);
    }
}

static void _c0_process_reliable(dongle_pkt_s *pkt)
{
    fifo_core0_reliable_push(&_cc_core0_reliable, pkt);
}

static void _c0_process_mailbox(void)
{
    dongle_pkt_s pkt;
    while(fifo_core0_mailbox_pop(&_cc_core0_mailbox, &pkt))
    {
        switch(pkt.id)
        {
            case DONGLE_PID_CORE_RELIABLE:
            _c0_process_reliable(&pkt);
            break;

            case DONGLE_PID_WAKE:
            _c0_process_wake(&pkt);
            break;

            default:
            break;
        }
    }
}

/* Once a wireless link has come UP, a later UP->DOWN edge means the gamepad
 * disconnected (powered off / out of range / timed out). Tearing down and
 * re-initializing USB in place does not reliably re-enumerate on the console
 * host, so the most robust recovery is a full chip reboot: it brings the USB
 * transport back up from scratch and restores the default boot session. */
static void _c0_reboot_on_link_down(void)
{
    static dongle_link_status_t prev_link = DONGLE_LINK_DOWN;
    dongle_link_status_t link = get_link_status();

    if(prev_link == DONGLE_LINK_UP && link == DONGLE_LINK_DOWN)
    {
        watchdog_reboot(0, 0, 0);
    }

    prev_link = link;
}

/* ========================================================================== */
/* Public Functions                                                           */
/* ========================================================================== */

void core0_task(void)
{
    dongle_rgb_task(time_us_64());
    _c0_process_mailbox();
    core_task(time_us_64());
    _c0_reboot_on_link_down();
}