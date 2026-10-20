/*
 * Switch 2 GameCube controller (NSO) core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_ns2gc.c
 * @brief The Switch 2's GameCube controller over USB, with the gamepad's input from the link.
 *
 * NS2-LIB-HID answers the console (vendor requests, bulk commands, pairing declined so the
 * controller takes no pairing slot). The input is the block the gamepad packs after its Slippi
 * report; the console's rumble goes back to the gamepad, which plays it as on a cable.
 */

#include "cores/core_ns2gc.h"

#include <stdio.h>
#include <string.h>

#include <dongle.h>
#include <dongle_host.h>

#include "pico/time.h"
#include "pico/unique_id.h"

#include "transport/transport.h"
#include "utilities/host_detect.h"

#if defined(HOJA_NS2)

#include "ns2_lib.h"
#include "ns2_lib_usb.h"

_Static_assert(DONGLE_NS2GC_BLOCK_LEN == NS2_GC_INPUT_BLOCK_LEN, "the link carries NS2-LIB-HID's block");

typedef enum
{
    NS2GC_HOST_UNKNOWN = 0,
    NS2GC_HOST_SWITCH2,
    NS2GC_HOST_SWITCH1,
    NS2GC_HOST_COMPUTER,
} ns2gc_host_t;

// A Switch 2 makes its vendor request about 2 ms after configuring; long after this, it isn't one
#define NS2GC_SWITCH2_WAIT_US 250000u

// A Switch 1 left in the middle of setting the device up backs off for seconds before it looks at
// the port again: it gets to finish (configured, or this long), then the restart is a plain unplug
#define NS2GC_SWITCH1_LEAVE_US 1000000u
#define NS2GC_SWITCH1_SETTLED_US 200000u

// Until power-off: the next session (Slippi again) is the device this host should see
static ns2gc_host_t _ns2gc_host = NS2GC_HOST_UNKNOWN;
static bool _ns2gc_running = false;
static bool _ns2gc_detecting = false;
static bool _ns2gc_device_seen = false;
static bool _ns2gc_configured = false;
static uint64_t _ns2gc_configured_us = 0;
static bool _ns2gc_leaving = false;
static uint64_t _ns2gc_decided_us = 0;

static core_params_s *_ns2gc_params = NULL;
static dongle_wake_s _ns2gc_wake;

// The gamepad's latest input block
static uint8_t _ns2gc_block[DONGLE_NS2GC_BLOCK_LEN];
static bool _ns2gc_block_valid = false;

// The rumble the gamepad was last told
static bool _ns2gc_rumble = false;

static uint64_t _ns2gc_now_us(void)
{
    return time_us_64();
}

static void _ns2gc_decide(ns2gc_host_t host)
{
    if (!_ns2gc_detecting)
    {
        return;
    }
    _ns2gc_detecting = false;
    _ns2gc_host = host;

    // A Switch 2 keeps this controller; anything else gets the adapter (core_ns2gc's task restarts
    // USB). A Switch 1 first finishes setting the device up.
    if (host != NS2GC_HOST_SWITCH2)
    {
        _ns2gc_leaving = true;
        _ns2gc_decided_us = _ns2gc_now_us();
    }
}

void core_ns2gc_usb_device_requested(uint16_t length)
{
    if (!_ns2gc_detecting || _ns2gc_device_seen)
    {
        return;
    }
    _ns2gc_device_seen = true;
    // Windows, Linux and macOS ask for 64 bytes first; a Switch asks for 8
    if (length > 8)
    {
        _ns2gc_decide(NS2GC_HOST_COMPUTER);
    }
}

void core_ns2gc_usb_string_requested(uint8_t index)
{
    (void)index;
    // A Switch 1 reads the language list before configuring; a Switch 2 reads nothing
    if (_ns2gc_detecting && !_ns2gc_configured)
    {
        _ns2gc_decide(NS2GC_HOST_SWITCH1);
    }
}

void core_ns2gc_usb_configured(void)
{
    if (!_ns2gc_detecting && !_ns2gc_leaving)
    {
        return;
    }
    _ns2gc_configured = true;
    _ns2gc_configured_us = _ns2gc_now_us();
}

bool core_ns2gc_wanted(const dongle_wake_s *wake)
{
    if (wake == NULL || !(wake->flags & DONGLE_WAKE_FLAG_NS2GC))
    {
        return false;
    }
    dongle_session_s session;
    dongle_session_unpack(wake->session, &session);
    dongle_host_kind_t kind;
    host_detect_get(&kind, NULL);
    return session.mode == DONGLE_MODE_SLIPPI && kind == DONGLE_HOST_SWITCH &&
           (_ns2gc_host == NS2GC_HOST_UNKNOWN || _ns2gc_host == NS2GC_HOST_SWITCH2);
}

bool core_ns2gc_running(void)
{
    return _ns2gc_running;
}

bool core_ns2gc_console_rate(void)
{
    if (_ns2gc_host == NS2GC_HOST_SWITCH1)
    {
        return true;
    }
    if (_ns2gc_host == NS2GC_HOST_COMPUTER)
    {
        return false;
    }
    dongle_host_kind_t kind;
    host_detect_get(&kind, NULL);
    return kind == DONGLE_HOST_SWITCH;
}

/* -------------------------------------------------------------------------- */
/* NS2-LIB-HID hooks                                                          */
/* -------------------------------------------------------------------------- */

void ns2_api_hook_get_input(ns2_input_s *out)
{
    // The live lane is the latest report (reading it again gives the same one)
    dongle_pkt_s pkt;
    if (dongle_api_host_transport_get_inputpacket(&pkt) &&
        pkt.len >= DONGLE_NS2GC_BLOCK_OFFSET + DONGLE_NS2GC_BLOCK_LEN)
    {
        memcpy(_ns2gc_block, &pkt.data[DONGLE_NS2GC_BLOCK_OFFSET], sizeof(_ns2gc_block));
        _ns2gc_block_valid = true;
    }

    // Nothing from the gamepad yet: the library's rest state (sticks centred)
    if (_ns2gc_block_valid && dongle_api_host_transport_get_link_status() == DONGLE_LINK_UP)
    {
        ns2_api_gc_input_unpack(_ns2gc_block, out);
    }
}

void ns2_api_hook_set_led(uint8_t led_mask, bool flashing)
{
    (void)flashing;
    uint8_t player = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (led_mask & (1u << i))
        {
            player = i + 1;
            break;
        }
    }
    dongle_api_host_transport_set_player(player);
}

// The gamepad plays the samples (its rumble code is the cable's)
void ns2_api_hook_play_vibration_sample(uint8_t sample_id)
{
    uint8_t r[64] = {0x0A, sample_id};
    dongle_api_host_transport_set_outputreport(r, 2);
}

static void _ns2gc_rumble_set(bool on)
{
    if (on == _ns2gc_rumble)
    {
        return;
    }
    _ns2gc_rumble = on;
    // The console's report 0x03 as the gamepad reads it: one rumble bit, or none
    uint8_t r[64] = {0x03, 0x00, on ? 0x01 : 0x00, 0x00, 0x00};
    dongle_api_host_transport_set_outputreport(r, 5);
}

/* -------------------------------------------------------------------------- */
/* USB personality                                                            */
/* -------------------------------------------------------------------------- */

static int16_t _ns2gc_vendor_request(uint8_t request, uint16_t value, uint8_t *out, uint16_t out_size)
{
    // The Switch 2's first request after configuring
    if (request == 0x03 && _ns2gc_configured)
    {
        _ns2gc_decide(NS2GC_HOST_SWITCH2);
    }
    return ns2_usb_vendor_request(request, value, out, out_size);
}

static void _ns2gc_command_rx(const uint8_t *data, uint16_t len)
{
    static uint8_t reply[NS2_USB_COMMAND_REPLY_MAX];
    uint16_t n = ns2_usb_process_command(data, len, reply, sizeof(reply));
    if (n)
    {
        hhl_tusb_ns2_reply(reply, n);
    }
}

static void _ns2gc_reset(void)
{
    ns2_usb_reset();
}

void core_ns2gc_usb_config(hhl_tusb_config_s *tcfg)
{
    tcfg->driver = HHL_TUSB_DRIVER_NS2;
    ns2_usb_get_descriptor_params(&tcfg->ns2.device_descriptor, &tcfg->ns2.config_descriptor,
                                  &tcfg->ns2.config_descriptor_len, &tcfg->ns2.report_descriptor,
                                  &tcfg->ns2.report_descriptor_len);
    tcfg->ns2.vendor_request = _ns2gc_vendor_request;
    tcfg->ns2.command_rx = _ns2gc_command_rx;
    tcfg->ns2.reset = _ns2gc_reset;
}

/* -------------------------------------------------------------------------- */
/* Core                                                                       */
/* -------------------------------------------------------------------------- */

// The USB transport starts only for a core with a device record (the driver takes the
// descriptors from core_ns2gc_usb_config)
static core_hid_device_t _ns2gc_hid_device = {
    .vid = 0x057E,
    .pid = 0x2073,
    .name = "GameCube Controller",
    .manufacturer = "Nintendo",
};

static bool _ns2gc_report(core_report_s *out)
{
    // Configured, and no vendor request from a Switch 2 came
    if (_ns2gc_detecting && _ns2gc_configured && (_ns2gc_now_us() - _ns2gc_configured_us) >= NS2GC_SWITCH2_WAIT_US)
    {
        _ns2gc_decide(NS2GC_HOST_SWITCH1);
    }

    uint16_t len = 0;
    if (!ns2_usb_generate_inputreport(out->data, &len))
    {
        return false;
    }
    out->reportformat = CORE_REPORTFORMAT_SLIPPI;
    out->size = len;
    return true;
}

// Output report 0x03 from a game: the GameCube's rumble state as 2-bit fields, rumble (low bit)
// and brake (high bit). Any rumble bit runs the motor; brake or nothing stops it.
static void _ns2gc_output_tunnel(const uint8_t *data, uint16_t len)
{
    if (len < 5 || data[0] != 0x03)
    {
        return;
    }
    _ns2gc_rumble_set(((data[2] | data[3] | data[4]) & 0x55) != 0);
}

static void _ns2gc_deinit(void)
{
    _ns2gc_rumble_set(false);
    _ns2gc_running = false;
    _ns2gc_detecting = false;
    _ns2gc_leaving = false;
}

static void _ns2gc_task(uint64_t timestamp)
{
    if (_ns2gc_params->core_transport_task)
    {
        _ns2gc_params->core_transport_task(timestamp);
    }

    if (!_ns2gc_leaving)
    {
        return;
    }
    uint64_t now = _ns2gc_now_us();
    bool settled = _ns2gc_configured && (now - _ns2gc_configured_us) >= NS2GC_SWITCH1_SETTLED_US;
    if (_ns2gc_host == NS2GC_HOST_COMPUTER || settled || (now - _ns2gc_decided_us) >= NS2GC_SWITCH1_LEAVE_US)
    {
        // The same session again: the host decided, it starts as the adapter
        _ns2gc_leaving = false;
        dongle_wake_s wake = _ns2gc_wake;
        core_deinit();
        sleep_ms(100);
        core_init(&wake);
    }
}

bool core_ns2gc_init(core_params_s *params, const dongle_wake_s *wake)
{
    _ns2gc_params = params;
    if (wake != NULL)
    {
        _ns2gc_wake = *wake;
    }

    // This dongle's own identity: the pairing is declined, so the console keeps nothing of it
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    ns2_device_config_s cfg = {0};
    cfg.type = NS2_DEVTYPE_GC;
    cfg.transport = NS2_TRANSPORT_USB;
    cfg.device_mac[0] = 0x98;
    memcpy(&cfg.device_mac[1], &id.id[3], 5);
    uint32_t serial = ((uint32_t)id.id[4] << 24) | ((uint32_t)id.id[5] << 16) | ((uint32_t)id.id[6] << 8) | id.id[7];
    snprintf((char *)cfg.device_serial, sizeof(cfg.device_serial), "HJW5%010lu", (unsigned long)serial);
    memset(&cfg.colors, 0xFF, sizeof(cfg.colors));
    cfg.usb_pairing = NS2_USB_PAIRING_DECLINE;
    if (ns2_api_init(&cfg) != NS2_CONFIG_OK)
    {
        return false;
    }

    // The retail controller reports every 4 ms
    params->core_pollrate_us = 4000;
    params->hid_device = &_ns2gc_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_SLIPPI;
    params->core_report_generator = _ns2gc_report;
    params->core_output_report_tunnel = _ns2gc_output_tunnel;
    params->core_deinit = _ns2gc_deinit;
    params->core_task = _ns2gc_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    // A host already found (a Switch 2) needs no finding out again
    _ns2gc_detecting = (_ns2gc_host == NS2GC_HOST_UNKNOWN);
    _ns2gc_device_seen = false;
    _ns2gc_configured = false;
    _ns2gc_leaving = false;
    _ns2gc_block_valid = false;
    _ns2gc_rumble = false;
    _ns2gc_running = true;

    return transport_init(params);
}

#else

bool core_ns2gc_wanted(const dongle_wake_s *wake)
{
    (void)wake;
    return false;
}

bool core_ns2gc_init(core_params_s *params, const dongle_wake_s *wake)
{
    (void)params;
    (void)wake;
    return false;
}

bool core_ns2gc_running(void)
{
    return false;
}

bool core_ns2gc_console_rate(void)
{
    dongle_host_kind_t kind;
    host_detect_get(&kind, NULL);
    return kind == DONGLE_HOST_SWITCH;
}

void core_ns2gc_usb_config(hhl_tusb_config_s *tcfg)
{
    (void)tcfg;
}

void core_ns2gc_usb_device_requested(uint16_t length)
{
    (void)length;
}

void core_ns2gc_usb_string_requested(uint8_t index)
{
    (void)index;
}

void core_ns2gc_usb_configured(void)
{
}

#endif
