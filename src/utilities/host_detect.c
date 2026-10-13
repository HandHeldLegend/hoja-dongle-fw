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
 *      N64 controller, so probes alone mean a GameCube. Meanwhile the NES/SNES port is
 *      watched: a powered NES or SNES holds latch low and clock high even before a game
 *      reads the controller, and pulses latch every frame once one does.
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

#include "pico/time.h"

#if !defined(HOJA_USB_ONLY)
#include "hal/joybus_n64_hal.h"
#include "hardware/gpio.h"
#endif

// Long enough for a console to start polling a controller it just found
#define HOST_DETECT_CONSOLE_MS 2000
// A computer asks for the BOS descriptor within milliseconds of its first request
#define HOST_DETECT_BOS_WAIT_MS 500
// No USB host by then: a charger or power bank
#define HOST_DETECT_USB_WAIT_MS 3000
// The dongle's own device on USB, until a gamepad connects: SInput, with the config app's interface
#define HOST_DETECT_DEVICE_NAME "HOJA Dongle"
// An NES or SNES latches 60 times a second, so this is a fraction of the listening time
#define HOST_DETECT_LATCH_MIN 10
// How long latch low and clock high must hold to count as a powered NES or SNES
#define HOST_DETECT_NESBUS_PRESENT_MS 50

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

static dongle_host_kind_t _kind = DONGLE_HOST_UNKNOWN;
static dongle_mode_t _mode = DONGLE_MODE_UNKNOWN;

#if defined(NESBUS_LATCH_PIN)
static volatile uint32_t _latch_count = 0;

static void _latch_irq_handler(void)
{
    if (gpio_get_irq_event_mask(NESBUS_LATCH_PIN) & GPIO_IRQ_EDGE_RISE)
    {
        gpio_acknowledge_irq(NESBUS_LATCH_PIN, GPIO_IRQ_EDGE_RISE);
        _latch_count++;
    }
}

// Count latch pulses while the N64 core listens on the data line. The board pulls latch and
// clock up, so both read high with nothing attached (or an N64, GameCube or USB host).
static void _latch_watch_start(void)
{
    _latch_count = 0;
    gpio_init(NESBUS_CLOCK_PIN);
    gpio_set_dir(NESBUS_CLOCK_PIN, GPIO_IN);
    gpio_disable_pulls(NESBUS_CLOCK_PIN);
    gpio_init(NESBUS_LATCH_PIN);
    gpio_set_dir(NESBUS_LATCH_PIN, GPIO_IN);
    gpio_disable_pulls(NESBUS_LATCH_PIN);
    gpio_add_raw_irq_handler(NESBUS_LATCH_PIN, _latch_irq_handler);
    gpio_set_irq_enabled(NESBUS_LATCH_PIN, GPIO_IRQ_EDGE_RISE, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

static uint32_t _latch_watch_stop(void)
{
    gpio_set_irq_enabled(NESBUS_LATCH_PIN, GPIO_IRQ_EDGE_RISE, false);
    gpio_remove_raw_irq_handler(NESBUS_LATCH_PIN, _latch_irq_handler);
    return _latch_count;
}

// A powered NES or SNES holds latch low and clock high. A console that is switched off pulls
// both low.
static bool _nesbus_console_present(uint64_t now_us)
{
    static uint64_t since_us = 0;

    if (gpio_get(NESBUS_LATCH_PIN) || !gpio_get(NESBUS_CLOCK_PIN))
    {
        since_us = 0;
        return false;
    }

    if (!since_us) since_us = now_us;
    return (now_us - since_us) >= (HOST_DETECT_NESBUS_PRESENT_MS * 1000ull);
}
#endif

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
    _kind = kind;
    _mode = mode;
    dongle_api_host_transport_set_detected(kind, mode);
    _set_phase(HOST_DETECT_DONE, now_us);
}

void host_detect_start(void)
{
    _usb_device = false;
    _usb_bos = false;
#if !defined(HOJA_USB_ONLY)
#if defined(NESBUS_LATCH_PIN)
    _latch_watch_start();
#endif
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
#if defined(NESBUS_LATCH_PIN)
        if (_nesbus_console_present(now_us))
        {
            _latch_watch_stop();
            core_deinit();
            core_init(_wake_for(DONGLE_MODE_SNES, NULL));
            _done(DONGLE_HOST_SNES, DONGLE_MODE_SNES, now_us);
            break;
        }
#endif

        if (elapsed_ms < HOST_DETECT_CONSOLE_MS)
        {
            break;
        }

        uint32_t probes, polls;
        joybus_n64_hal_get_counts(&probes, &polls);

        uint32_t latches = 0;
#if defined(NESBUS_LATCH_PIN)
        latches = _latch_watch_stop();
#endif

        if (polls > 0)
        {
            _done(DONGLE_HOST_N64, DONGLE_MODE_N64, now_us);
        }
        else if (latches >= HOST_DETECT_LATCH_MIN)
        {
            core_deinit();
            core_init(_wake_for(DONGLE_MODE_SNES, NULL));
            _done(DONGLE_HOST_SNES, DONGLE_MODE_SNES, now_us);
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
            core_init(_wake_for(DONGLE_MODE_SINPUT, HOST_DETECT_DEVICE_NAME));
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

void host_detect_get(dongle_host_kind_t *kind, dongle_mode_t *mode)
{
    if (kind) *kind = _kind;
    if (mode) *mode = _mode;
}

bool host_detect_is_pc(void)
{
    return _phase == HOST_DETECT_DONE && _kind == DONGLE_HOST_PC;
}

bool host_detect_set_pc_mode(dongle_mode_t mode)
{
    if (!host_detect_is_pc())
    {
        return false;
    }

    // Not XInput: Windows' Xbox 360 driver takes the whole device, config interface and all, so the
    // config app couldn't change the mode back
    switch (mode)
    {
    case DONGLE_MODE_SWITCH:
    case DONGLE_MODE_SINPUT:
    case DONGLE_MODE_SLIPPI:
        _mode = mode;
        dongle_api_host_transport_set_detected(_kind, _mode);
        return true;

    default:
        return false;
    }
}

bool host_detect_link_lost(void)
{
    if (!host_detect_is_pc())
    {
        return false;
    }

    // As at a gamepad's arrival: the host has to see the device leave before the new one comes
    core_deinit();
    sleep_ms(100);
    core_init(_wake_for(DONGLE_MODE_SINPUT, HOST_DETECT_DEVICE_NAME));
    return true;
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
