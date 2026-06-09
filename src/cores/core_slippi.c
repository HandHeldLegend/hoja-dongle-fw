/*
 * Slippi core: emulates a Nintendo GameCube USB adapter for Slippi/melee.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_slippi.c
 * @brief Official Nintendo GameCube Adapter (Slippi) USB personality.
 *
 * USB descriptors are owned by HHL-TINYUSB-DRIVERS. This core builds the
 * adapter-style input report framing and bridges wireless data to the host.
 */

#include <dongle.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "hhl_tusb_slippi.h"

#include "cores/core_slippi.h"

#include "cores/cores.h"

#include <dongle_host.h>
#include "transport/transport.h"

#define SLIPPI_DEFAULT_NAME "GameCube Adapter"
#define SLIPPI_DEFAULT_MFG  "Nintendo"

/* Strings only — Slippi descriptors come from HHL-TINYUSB-DRIVERS. */
static core_hid_device_t _slippi_hid_device = {
    .vid = HHL_TUSB_SLIPPI_VID,
    .pid = HHL_TUSB_SLIPPI_PID,
    .name = SLIPPI_DEFAULT_NAME,
    .manufacturer = SLIPPI_DEFAULT_MFG,
};

/**
 * @brief Handle host output reports from the GameCube adapter protocol.
 */
void _core_slippi_output_tunnel(const uint8_t *data, uint16_t len)
{
    switch (data[0])
    {
    case 0x11:
    {
        uint8_t strength = (data[1] & 0x1) ? 255 : 0;
        (void)strength;
        break;
    }

    case 0x13:
        break;

    default:
        break;
    }

    (void)len;
}

/**
 * @brief Build the 37-byte GameCube adapter input report.
 */
bool _core_slippi_get_generated_report(core_report_s *out)
{
    static bool _slippi_first = false;

    out->reportformat = CORE_REPORTFORMAT_SLIPPI;
    out->size = 37;

    out->data[0] = 0x21;
    out->data[1] = 0x14;
    out->data[10] = 0x04;
    out->data[19] = 0x04;
    out->data[28] = 0x04;

    if (!_slippi_first)
    {
        _slippi_first = true;
        return true;
    }

    if (dongle_api_host_transport_get_link_status() == DONGLE_LINK_UP)
    {
        dongle_pkt_s pkt;
        if (dongle_api_host_transport_get_inputpacket(&pkt) && pkt.len > 0)
        {
            uint16_t n = pkt.len > out->size ? out->size : pkt.len;
            memcpy(out->data, pkt.data, n);
        }
    }
    return true;
}

core_params_s *_slippi_core_params = NULL;

/** @brief Stop the USB transport when the core is torn down. */
void _core_slippi_deinit(void)
{
}

/** @brief Per-tick servicing of the USB transport. */
void _core_slippi_task(uint64_t timestamp)
{
    if (_slippi_core_params->core_transport_task)
    {
        _slippi_core_params->core_transport_task(timestamp);
    }
}

/* Populate params with Slippi callbacks and start USB. */
bool core_slippi_init(core_params_s *params, const dongle_wake_s *wake)
{
    _slippi_core_params = params;

    core_hid_apply_wake_identity(wake, &_slippi_hid_device, SLIPPI_DEFAULT_NAME, SLIPPI_DEFAULT_MFG);

    params->core_pollrate_us = 1000;
    params->hid_device = &_slippi_hid_device;

    params->core_report_format = CORE_REPORTFORMAT_SLIPPI;
    params->core_report_generator = _core_slippi_get_generated_report;
    params->core_output_report_tunnel = _core_slippi_output_tunnel;
    params->core_deinit = _core_slippi_deinit;
    params->core_task = _core_slippi_task;

    params->core_transport = GAMEPAD_TRANSPORT_USB;

    return transport_init(params);
}
