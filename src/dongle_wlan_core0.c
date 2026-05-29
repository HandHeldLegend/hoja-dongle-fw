#include "dongle_wlan_core0.h"

#include <string.h>

#include "dongle_wlan.h"
#include "cores/cores.h"
#include "utilities/crosscore_snapshot.h"

#include "pico/bootrom.h"
#include "hardware/watchdog.h"

/*
 * Core 0 WLAN
 * -----------
 * - Owns dongle_status_u (rumble, player, connection) for STATUS packets on core 1.
 * - Consumes the WAKE mailbox and calls core_init() when the gamepad selects a mode.
 * - Handles link-timeout recovery (restore boot core, usually N64).
 *
 * WAKE / core_init path (see dongle_wlan_wake_post / wake_consume):
 *   main loop → dongle_wlan_core0_poll() → try_consume_wake_and_init_core()
 */

static dongle_status_u _status;
static uint8_t _boot_format = 0xFF;
static uint8_t _active_format = 0xFF;
static bool _wlan_owned_core;
static dongle_session_s _active_session;

SNAPSHOT_TYPE(wlan_status, dongle_status_u);
static snapshot_wlan_status_t _snap_status;

SNAPSHOT_TYPE(wlan_link_timeout, uint8_t);
static snapshot_wlan_link_timeout_t _snap_link_timeout;

static void status_refresh(void)
{
    snapshot_wlan_status_write(&_snap_status, &_status);
}

static core_reportformat_t mode_to_format(dongle_mode_t mode)
{
    switch (mode)
    {
    case DONGLE_MODE_SINPUT:
        return CORE_REPORTFORMAT_SINPUT;
    case DONGLE_MODE_XINPUT:
        return CORE_REPORTFORMAT_XINPUT;
    case DONGLE_MODE_SLIPPI:
        return CORE_REPORTFORMAT_SLIPPI;
    case DONGLE_MODE_SNES:
        return CORE_REPORTFORMAT_SNES;
    case DONGLE_MODE_N64:
        return CORE_REPORTFORMAT_N64;
    case DONGLE_MODE_GAMECUBE:
        return CORE_REPORTFORMAT_GAMECUBE;
    case DONGLE_MODE_SWITCH:
        return CORE_REPORTFORMAT_SWPRO;
    default:
        return CORE_REPORTFORMAT_UNDEFINED;
    }
}

/*
 * Switch the active core to match the gamepad WAKE payload.
 * Called only after dongle_wlan_wake_consume() returns true (one shot per posted WAKE).
 */
static void init_core_from_wake(const dongle_wake_s *wake, const dongle_session_s *session)
{
    core_reportformat_t fmt = mode_to_format((dongle_mode_t)wake->mode);
    if (fmt == CORE_REPORTFORMAT_UNDEFINED)
    {
        return;
    }

    bool format_changed = _active_format != (uint8_t)fmt;

    /*
     * USB cores cannot hot-switch: reboot when leaving an active non-N64 core.
     * Boot N64 is allowed to transition to a wlan-selected mode without reboot.
     */
    if (format_changed && _active_format != (uint8_t)CORE_REPORTFORMAT_UNDEFINED &&
        _active_format != (uint8_t)CORE_REPORTFORMAT_N64)
    {
        watchdog_reboot(0, 0, 0);
        return;
    }

    core_deinit();
    if (!core_init(fmt, wake))
    {
        return;
    }

    _active_session = *session;
    _active_format = (uint8_t)fmt;
    _wlan_owned_core = (fmt != (core_reportformat_t)_boot_format);
    if (fmt == CORE_REPORTFORMAT_N64 && fmt == (core_reportformat_t)_boot_format)
    {
        _wlan_owned_core = false;
    }
}

/*
 * If core 1 posted a new WAKE since the last consume, apply it now.
 */
static void try_consume_wake_and_init_core(void)
{
    dongle_wake_s wake;
    dongle_session_s session;

    if (!dongle_wlan_wake_consume(&wake, &session))
    {
        return;
    }

    init_core_from_wake(&wake, &session);
}

static void handle_link_timeout(void)
{
    if (_wlan_owned_core)
    {
        core_deinit();
        core_init((core_reportformat_t)_boot_format, NULL);
        _active_format = _boot_format;
        _wlan_owned_core = false;
    }

    dongle_wlan_core0_reset();
    dongle_update_connection_status(DONGLE_CONN_IDLE);
}

void dongle_wlan_core0_init(void)
{
    memset(&_status, 0, sizeof(_status));
    _status.connection = DONGLE_CONN_IDLE;
    status_refresh();
    dongle_wlan_core0_reset();
}

void dongle_wlan_core0_reset(void)
{
    memset(&_active_session, 0, sizeof(_active_session));
    dongle_wlan_reset();
}

void dongle_wlan_core0_set_boot_format(uint8_t format)
{
    _boot_format = format;
    _active_format = format;
}

void dongle_wlan_core0_signal_link_timeout(void)
{
    uint8_t flag = 1;
    snapshot_wlan_link_timeout_write(&_snap_link_timeout, &flag);
}

void dongle_wlan_core0_status_snapshot_read(dongle_status_u *out)
{
    if (out)
    {
        snapshot_wlan_status_read(&_snap_status, out);
    }
}

void dongle_wlan_core0_poll(uint64_t now_us)
{
    (void)now_us;

    uint8_t timeout = 0;
    snapshot_wlan_link_timeout_read(&_snap_link_timeout, &timeout);
    if (timeout)
    {
        timeout = 0;
        snapshot_wlan_link_timeout_write(&_snap_link_timeout, &timeout);
        handle_link_timeout();
        return;
    }

    try_consume_wake_and_init_core();
}
