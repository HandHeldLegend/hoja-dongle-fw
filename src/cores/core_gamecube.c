/*
 * Nintendo GameCube controller core: Joybus personality and report bridging.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_gamecube.c
 * @brief Nintendo GameCube controller personality over the Joybus transport.
 *
 * Speaks the console-side GameCube Joybus protocol (GAMEPAD_TRANSPORT_JOYBUSGC)
 * directly rather than USB. It provides the controller input report to the
 * transport on demand, sourcing live data from core0's unreliable wireless lane
 * and falling back to the last report when no fresh packet is available.
 */

#include "cores/core_gamecube.h"

#include <string.h>

#include "core0transport.h"
#include "transport/transport.h"

/* Cached last report so polls still return valid data when no fresh packet arrives. */
static core_gamecube_report_s _last_report;

/* Build the GameCube input report from the freshest packet, else repeat last one. */
bool _core_gamecube_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_GAMECUBE;
    out->size = sizeof(core_gamecube_report_s);

    dongle_pkt_s pkt;
    if (core0_get_unreliable_inputreport(&pkt) && pkt.len == out->size)
    {
        memcpy(&_last_report, pkt.data, pkt.len);
        memcpy(out->data, &_last_report, out->size);
    }
    else
    {
        memcpy(out->data, &_last_report, out->size);
    }
    return true;
}

core_params_s *_gamecube_core_params = NULL;

/* Drive the Joybus transport task each tick (GameCube has no USB transport). */
void _core_gamecube_task(uint64_t timestamp)
{
    if (_gamecube_core_params->core_transport_task)
    {
        _gamecube_core_params->core_transport_task(timestamp);
    }
}

/* Populate params with GameCube callbacks and start the Joybus transport. */
bool core_gamecube_init(core_params_s *params)
{
    _gamecube_core_params = params;

    params->core_pollrate_us = 1000;

    params->core_report_format = CORE_REPORTFORMAT_GAMECUBE;
    params->core_report_generator = _core_gamecube_get_generated_report;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_gamecube_task;

    params->core_transport = GAMEPAD_TRANSPORT_JOYBUSGC;

    return transport_init(params);
}
