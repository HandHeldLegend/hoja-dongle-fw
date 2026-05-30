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

#include "core0transport.h"
#include "core1wlan.h"

#include "utilities/rgb.h"
#include "utilities/crosscore_snapshot.h"
#include "utilities/crosscore_fifo.h"

#include "cores/cores.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#define CORE0_MAILBOX_LEN 16

// Current operational status (core 0 is the single writer)
static dongle_status_u _this_status = {0};
static dongle_wake_s _this_wake = {0};

// Outbound status snapshot — core 0 writes, core 1 reads via core0_get_status()
SNAPSHOT_TYPE(core0_status, dongle_status_u);
static snapshot_core0_status_t _ss_core0_status;

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

// Update status crosscore data
static void _c0_publish_status(void)
{
    snapshot_core0_status_write(&_ss_core0_status, &_this_status);
}

// Send a data
// packet from core 1 to be processed in core 0
void core0_send_pkt(const dongle_pkt_s *pkt)
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
bool core0_get_unreliable_inputreport(dongle_pkt_s *pkt)
{
    if(!pkt) return false;
    snapshot_core0_unreliable_read(&_ss_core0_unreliable, pkt);
    return pkt->len > 0;
}

// Pop one queued CORE_RELIABLE IN report (gamepad -> host)
bool core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len)
{
    dongle_pkt_s pkt;
    if(!data || !len) return false;
    if(!fifo_core0_reliable_pop(&_cc_core0_reliable, &pkt)) return false;

    memcpy(data, pkt.data, pkt.len);
    *len = pkt.len;
    return true;
}

// Queue a reliable host -> gamepad OUT report for core 1 to transmit
void core0_send_reliable_outputreport(const uint8_t *data, uint16_t len)
{
    if(!data || len == 0 || len > 64) return;

    dongle_pkt_s pkt = {0};
    pkt.id = DONGLE_PID_CORE_RELIABLE;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    core1_send_pkt(&pkt);
}

// Queue a reliable host -> gamepad OUT bulk report for core 1 to transmit
void core0_send_reliable_configreport(const uint8_t *data, uint16_t len)
{
    if(!data || len == 0 || len > 64) return;

    dongle_pkt_s pkt = {0};
    pkt.id = DONGLE_PID_CONFIG_RELIABLE;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    core1_send_pkt(&pkt);
}

void core0_set_rumble(uint8_t left, uint8_t right, uint8_t brake_left, uint8_t brake_right)
{
    _this_status.rumble.left = left;
    _this_status.rumble.right = right;
    _this_status.brake.left = brake_left;
    _this_status.brake.right = brake_right;
    _c0_publish_status();
}

void core0_set_player_number(uint8_t player)
{
    _this_status.player_number = player;
    _c0_publish_status();
}

// Dongle->console transport state (USB active / joybus connected). Core 0 owned.
void core0_set_transport_status(dongle_transport_status_t status)
{
    if(_this_status.transport_status == (uint8_t)status) return;
    _this_status.transport_status = (uint8_t)status;
    _c0_publish_status();
}

void core0_get_status(dongle_status_u *out)
{
    if(!out) return;
    snapshot_core0_status_read(&_ss_core0_status, out);
}

// Core 1 reports the WLAN link state via a STATUS packet. Only the link_status
// field is core1-owned; transport_status/rumble/player stay authoritative on core 0.
static void _c0_process_status(dongle_pkt_s *pkt)
{
    if(pkt->len != sizeof(dongle_status_u)) return;

    dongle_status_u tmp;
    memcpy(&tmp, pkt->data, pkt->len);

    if(tmp.link_status != _this_status.link_status)
    {
        _this_status.link_status = tmp.link_status;
        _c0_publish_status();
    }
}

static void _c0_process_wake(dongle_pkt_s *pkt)
{
    if(pkt->len != sizeof(dongle_wake_s)) return;

    dongle_wake_s tmp;
    memcpy(&tmp, pkt->data, pkt->len);

    if(tmp.session != _this_wake.session)
    {
        // Update our wake data
        _this_wake = tmp;

        // Deinit core (only applies if it's running too)
        core_deinit();

        // core_init applies the matching USB mode + sets the mode LED.
        core_init(&_this_wake);
    }
}

static void _c0_process_reliable(dongle_pkt_s *pkt)
{
    fifo_core0_reliable_push(&_cc_core0_reliable, pkt);
}

static void _c0_process_mailboxes(void)
{
    dongle_pkt_s pkt;
    while(fifo_core0_mailbox_pop(&_cc_core0_mailbox, &pkt))
    {
        switch(pkt.id)
        {
            case DONGLE_PID_CORE_RELIABLE:
            _c0_process_reliable(&pkt);
            break;

            case DONGLE_PID_STATUS:
            _c0_process_status(&pkt);
            break;

            case DONGLE_PID_WAKE:
            _c0_process_wake(&pkt);
            break;

            default:
            break;
        }
    }
}

static void core0_task(uint64_t time)
{
    dongle_rgb_task(time);
    _c0_process_mailboxes();
    core_task(time);
}

int main(void)
{
    dongle_rgb_enter_bootloader_if_buttons_held();

    stdio_init_all();

    core_init(core_boot_wake());

    multicore_launch_core1(core1_entry);

    // CYW43 may reconfigure GPIOs at init; reclaim button pins on core0.
    dongle_rgb_gpio_init();
    dongle_rgb_init();

    for(;;)
    {
        core0_task(time_us_64());
    }
}
