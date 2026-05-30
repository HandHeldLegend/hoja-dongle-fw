/*
 * Core dispatcher: selects and drives the active gamepad personality.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file cores.c
 * @brief Active-core selection and lifecycle dispatch for gamepad personalities.
 *
 * A "core" is a console/USB personality (Switch, XInput, N64, GameCube, etc.)
 * that produces input reports and consumes output reports. This file owns the
 * single active core_params_s instance and routes generic core_* calls
 * (init/task/deinit, report generation, input tunneling) to whichever core was
 * selected by the boot wake mode. Each concrete core fills in the params
 * callbacks during its own init.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cores/cores.h"
#include "transport/transport.h"

#include "utilities/rgb.h"

#include "cores/core_sinput.h"
#include "cores/core_slippi.h"
#include "cores/core_n64.h"
#include "cores/core_gamecube.h"
#include "cores/core_xinput.h"
#include "cores/core_switch.h"

/* Pristine defaults; core_deinit() restores _core_params from this. */
const core_params_s _core_params_default = {
    .core_report_format = CORE_REPORTFORMAT_UNDEFINED,
    .core_pollrate_us = 8000,
    .core_report_generator = NULL,
    .core_output_report_tunnel = NULL,
    .core_deinit = NULL,
    .core_transport = GAMEPAD_TRANSPORT_UNDEFINED,
    .core_transport_task = NULL,
    .hid_device = NULL,
};

/* The single live core configuration, populated by the selected core's init. */
core_params_s _core_params = {
    .core_report_format = CORE_REPORTFORMAT_UNDEFINED,
    .core_pollrate_us = 8000,
    .core_transport = GAMEPAD_TRANSPORT_UNDEFINED,
};

/* Ask the active core to build its next input report (no-op if unset). */
bool core_get_generated_report(core_report_s *out)
{
    if (!_core_params.core_report_generator)
    {
        return false;
    }
    return _core_params.core_report_generator(out);
}

/* Expose the live params so the transport layer can read callbacks/format. */
core_params_s *core_current_params(void)
{
    return &_core_params;
}

/* Default mode used when the dongle boots before a host has selected one. */
static const dongle_session_s _boot_session = {
    .mode = DONGLE_MODE_N64, .id = 0
};

static dongle_wake_s _boot_wake;

/* Synthesize the boot wake (default N64 session) used before the host pairs. */
const dongle_wake_s *core_boot_wake(void)
{
    _boot_wake.session = dongle_session_pack(&_boot_session);
    return &_boot_wake;
}

/* Select and initialize the core matching the wake session's mode. */
bool core_init(const dongle_wake_s *wake)
{
    if (!wake)
    {
        return false;
    }

    dongle_session_s session;
    dongle_session_unpack(wake->session, &session);

    /* Reflect the selected mode on the status LED before bringing the core up. */
    dongle_rgb_set_mode(session.mode);

    switch ((dongle_mode_t)session.mode)
    {
    case DONGLE_MODE_SINPUT:
        return core_sinput_init(&_core_params, wake);

    case DONGLE_MODE_XINPUT:
        return core_xinput_init(&_core_params, wake);

    case DONGLE_MODE_SWITCH:
        return core_switch_init(&_core_params, wake);

    case DONGLE_MODE_N64:
        return core_n64_init(&_core_params);

    case DONGLE_MODE_GAMECUBE:
        return core_gamecube_init(&_core_params);

    case DONGLE_MODE_SLIPPI:
        return core_slippi_init(&_core_params, wake);

    case DONGLE_MODE_SNES:
    default:
        /* SNES is not implemented; unknown modes fail rather than guess. */
        return false;
    }
}

/* Drive the active core's periodic work (per-poll transport/report servicing). */
void core_task(uint64_t timestamp)
{
    if (_core_params.core_task)
    {
        _core_params.core_task(timestamp);
    }
}

/* Tear down the active core, stop the transport, and reset to pristine defaults. */
void core_deinit(void)
{
    if (_core_params.core_deinit)
    {
        _core_params.core_deinit();
    }
    transport_stop();
    _core_params = _core_params_default;
}
