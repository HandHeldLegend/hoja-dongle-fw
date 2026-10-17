/*
 * NES/SNES controller core: NESBus personality and report bridging.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_snes.c
 * @brief NES/SNES controller personality over the NESBus transport.
 *
 * Like the N64 core, this speaks to the console directly
 * (GAMEPAD_TRANSPORT_NESBUS) rather than over USB. Button reports are sourced
 * from the link; the transport works out per latch whether an NES or an
 * SNES is reading it.
 */

#include <stdlib.h>
#include <string.h>
#include <hoja_types.h>

#include "cores/core_snes.h"
#include <dongle_host.h>
#include "transport/transport.h"

#define CORE_SNES_DEFAULT_NAME "SNES Controller"
#define CORE_SNES_DEFAULT_MFG  "Nintendo"

static core_hid_device_t _snes_dongle_hid = {
    .name = CORE_SNES_DEFAULT_NAME,
    .manufacturer = CORE_SNES_DEFAULT_MFG,
};

/* Cached last report so latches still return valid data when no fresh packet arrives.
 * Starts with every button released (active low). */
static core_snes_report_s _last_report = {.value = 0xFFFFFFFF};

/* Build the SNES report from the freshest report from the gamepad, else repeat the last one. */
bool _core_snes_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_SNES;
    out->size = sizeof(core_snes_report_s);

    dongle_pkt_s pkt;
    if (dongle_api_host_transport_get_inputpacket(&pkt) && pkt.len == out->size)
    {
        memcpy(&_last_report, pkt.data, pkt.len);
    }
    memcpy(out->data, &_last_report, out->size);
    return true;
}

core_params_s *_snes_core_params = NULL;

/* Drive the NESBus transport task each tick (SNES has no USB transport). */
void _core_snes_task(uint64_t timestamp)
{
    if (_snes_core_params->core_transport_task)
    {
        _snes_core_params->core_transport_task(timestamp);
    }
}

/* Populate params with SNES callbacks and start the NESBus transport. */
bool core_snes_init(core_params_s *params, const dongle_wake_s *wake)
{
    _snes_core_params = params;
    _last_report.value = 0xFFFFFFFF;

    core_hid_apply_wake_identity(wake, &_snes_dongle_hid, CORE_SNES_DEFAULT_NAME, CORE_SNES_DEFAULT_MFG);
    params->hid_device = &_snes_dongle_hid;

    params->core_pollrate_us = 1000;
    params->core_report_format = CORE_REPORTFORMAT_SNES;
    params->core_report_generator = _core_snes_get_generated_report;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_snes_task;
    params->core_transport = GAMEPAD_TRANSPORT_NESBUS;

    return transport_init(params);
}
