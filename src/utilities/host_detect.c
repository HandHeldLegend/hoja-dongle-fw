/*
 * What the dongle is plugged into, for gamepads that follow it.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file host_detect.c
 * @brief Finds the host at power-up and publishes it for the beacon.
 *
 * Order, console first (the HOJA board shares one port between the console and USB):
 *   1. N64 core listening: N64 polls mean an N64. A GameCube probes but never polls an
 *      N64 controller, so probes alone mean a GameCube.
 *   2. USB as a placeholder SInput device: computers ask for the BOS descriptor while
 *      connecting, a Switch never does.
 * Gamepads only hear beacons once this is done, so they follow the right mode.
 */

#include "utilities/host_detect.h"

#include <string.h>

#include <dongle.h>
#include <dongle_host.h>

#include "cores/cores.h"
#include "dongle_network.h"

#if !defined(HOJA_USB_ONLY)
#include "hal/joybus_n64_hal.h"
#endif

// Long enough for a console to start polling a controller it just found
#define HOST_DETECT_CONSOLE_MS 2000
// A computer asks for the BOS descriptor within milliseconds of its first request
#define HOST_DETECT_BOS_WAIT_MS 500
// No USB host by then: a charger or power bank
#define HOST_DETECT_USB_WAIT_MS 3000

typedef enum
{
    HOST_DETECT_OFF,
    HOST_DETECT_CONSOLE, // Listening as an N64
    HOST_DETECT_WAIT_AP, // USB waits for the radio: starting both at once upsets WLAN start-up
    HOST_DETECT_USB,     // Enumerated as the placeholder, watching the host's requests
    HOST_DETECT_DONE,
} host_detect_phase_t;

static host_detect_phase_t _phase = HOST_DETECT_OFF;
static uint64_t _phase_us = 0;

static volatile bool _usb_device = false;
static volatile uint64_t _usb_device_us = 0;
static volatile bool _usb_bos = false;

static dongle_wake_s _detect_wake;

static const dongle_wake_s *_wake_for(dongle_mode_t mode, const char *name)
{
    dongle_session_s session = {.mode = mode, .id = 0};
    memset(&_detect_wake, 0, sizeof(_detect_wake));
    _detect_wake.session = dongle_session_pack(&session);
    dongle_wake_strcopy(_detect_wake.name, DONGLE_WAKE_NAME_LEN, name);
    return &_detect_wake;
}

static void _set_phase(host_detect_phase_t phase, uint64_t now_us)
{
    _phase = phase;
    _phase_us = now_us;
}

static void _done(dongle_host_kind_t kind, dongle_mode_t mode, uint64_t now_us)
{
    dongle_api_host_transport_set_detected(kind, mode);
    _set_phase(HOST_DETECT_DONE, now_us);
}

void host_detect_start(void)
{
    _usb_device = false;
    _usb_bos = false;
#if !defined(HOJA_USB_ONLY)
    _set_phase(HOST_DETECT_CONSOLE, 0);
#else
    _set_phase(HOST_DETECT_WAIT_AP, 0);
#endif
}

void host_detect_task(uint64_t now_us)
{
    if (_phase_us == 0)
    {
        _phase_us = now_us;
    }
    const uint64_t elapsed_ms = (now_us - _phase_us) / 1000;

    switch (_phase)
    {
    default:
    case HOST_DETECT_OFF:
    case HOST_DETECT_DONE:
        break;

#if !defined(HOJA_USB_ONLY)
    case HOST_DETECT_CONSOLE:
    {
        if (elapsed_ms < HOST_DETECT_CONSOLE_MS)
        {
            break;
        }

        uint32_t probes, polls;
        joybus_n64_hal_get_counts(&probes, &polls);

        if (polls > 0)
        {
            _done(DONGLE_HOST_N64, DONGLE_MODE_N64, now_us);
        }
        else if (probes > 0)
        {
            core_deinit();
            core_init(_wake_for(DONGLE_MODE_GAMECUBE, NULL));
            _done(DONGLE_HOST_GAMECUBE, DONGLE_MODE_GAMECUBE, now_us);
        }
        else
        {
            core_deinit();
            _set_phase(HOST_DETECT_WAIT_AP, now_us);
        }
        break;
    }
#endif

    case HOST_DETECT_WAIT_AP:
        if (dongle_network_ap_is_ready())
        {
            _usb_device = false;
            _usb_bos = false;
            core_init(_wake_for(DONGLE_MODE_SINPUT, "HOJA Dongle"));
            _set_phase(HOST_DETECT_USB, now_us);
        }
        break;

    case HOST_DETECT_USB:
        if (_usb_bos)
        {
            _done(DONGLE_HOST_PC, DONGLE_MODE_SINPUT, now_us);
        }
        else if (_usb_device && (now_us - _usb_device_us) >= (HOST_DETECT_BOS_WAIT_MS * 1000ull))
        {
            _done(DONGLE_HOST_SWITCH, DONGLE_MODE_SWITCH, now_us);
        }
        else if (!_usb_device && elapsed_ms >= HOST_DETECT_USB_WAIT_MS)
        {
            // Nothing to follow: gamepads keep their own mode
            _done(DONGLE_HOST_UNKNOWN, DONGLE_MODE_UNKNOWN, now_us);
        }
        break;
    }
}

void host_detect_on_usb_device(void)
{
    if (!_usb_device)
    {
        _usb_device_us = dongle_api_hook_get_time_us_u64();
        _usb_device = true;
    }
}

void host_detect_on_usb_bos(void)
{
    _usb_bos = true;
}
