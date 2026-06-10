/*
 * Nintendo 64 controller core: Joybus personality and report bridging.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_n64.c
 * @brief Nintendo 64 controller personality over the Joybus transport.
 *
 * Unlike the USB cores, the N64 core speaks the console-side Joybus protocol
 * (GAMEPAD_TRANSPORT_JOYBUS64) rather than USB. Input reports are sourced from
 * the WLAN link; rumble and transport status are forwarded back to the gamepad
 * via the dongle STATUS channel.
 */

#include <stdlib.h>
#include <string.h>
#include <hoja_types.h>

#include "cores/core_n64.h"
#include <dongle_host.h>
#include "transport/transport.h"

#define CORE_N64_DEFAULT_NAME "N64 Controller"
#define CORE_N64_DEFAULT_MFG  "Nintendo"

static core_hid_device_t _n64_wlan_hid = {
    .name = CORE_N64_DEFAULT_NAME,
    .manufacturer = CORE_N64_DEFAULT_MFG,
};

/* Cached last report so polls still return valid data when no fresh packet arrives. */
static core_n64_report_s _last_report;

/* Build the N64 input report from the freshest WLAN packet, else repeat the last one. */
bool _core_n64_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_N64;
    out->size = sizeof(core_n64_report_s);

    dongle_pkt_s pkt;
    if (dongle_api_host_transport_get_inputpacket(&pkt) && pkt.len == out->size)
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

core_params_s *_n64_core_params = NULL;

/* Drive the Joybus transport task each tick (N64 has no USB transport). */
void _core_n64_task(uint64_t timestamp)
{
    if (_n64_core_params->core_transport_task)
    {
        _n64_core_params->core_transport_task(timestamp);
    }
}

/* Populate params with N64 callbacks and start the Joybus transport. */
bool core_n64_init(core_params_s *params, const dongle_wake_s *wake)
{
    _n64_core_params = params;

    core_hid_apply_wake_identity(wake, &_n64_wlan_hid, CORE_N64_DEFAULT_NAME, CORE_N64_DEFAULT_MFG);
    params->hid_device = &_n64_wlan_hid;

    params->core_pollrate_us = 1000;
    params->core_report_format = CORE_REPORTFORMAT_N64;
    params->core_report_generator = _core_n64_get_generated_report;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_n64_task;
    params->core_transport = GAMEPAD_TRANSPORT_JOYBUS64;

    return transport_init(params);
}
