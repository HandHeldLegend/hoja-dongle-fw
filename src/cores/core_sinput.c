/*
 * SInput gamepad core: USB HID personality with command/feature reports.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_sinput.c
 * @brief SInput USB HID gamepad personality with command/feature channel.
 *
 * Presents a standard HID gamepad (the "SInput" protocol) to the host. In
 * addition to the regular input report, SInput multiplexes command/data
 * reports: the host can request haptics, player LED, or a one-shot device
 * "features" descriptor. Output command reports are relayed to the gamepad over
 * core0's reliable lane, and the features reply is captured from the reliable
 * input lane and surfaced as the next input report.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <dongle.h>
#include <dongle_host.h>

#include <sinput_lib.h>
#include <sinput_lib_hid.h>

#include "cores/cores.h"
#include "transport/transport.h"

#define SINPUT_DEFAULT_VID 0x2E8A
#define SINPUT_DEFAULT_PID 0x10C6
#define SINPUT_NAME "SInput Gamepad"

static core_params_s *_sinput_params = NULL;

static core_hid_device_t _sinput_hid_device = {
    .config_descriptor = NULL,
    .config_descriptor_len = 0,
    .hid_report_descriptor = NULL,
    .hid_report_descriptor_len = 0,
    .device_descriptor = NULL,
    .name = SINPUT_NAME,
    .pid = SINPUT_DEFAULT_PID,
    .vid = SINPUT_DEFAULT_VID,
};

/** @brief Override descriptor VID/PID/name from the gamepad WAKE packet. */
static void _sinput_apply_wake(const dongle_wake_s *wake)
{
    if (wake->vid)
    {
        _sinput_hid_device.vid = wake->vid;
    }
    if (wake->pid)
    {
        _sinput_hid_device.pid = wake->pid;
    }

    if (wake->name[0] != '\0')
    {
        memcpy(_sinput_hid_device.name, wake->name, sizeof(wake->name));
        _sinput_hid_device.name[sizeof(wake->name)] = '\0';
    }
    else
    {
        strncpy(_sinput_hid_device.name, SINPUT_NAME, sizeof(_sinput_hid_device.name) - 1);
        _sinput_hid_device.name[sizeof(_sinput_hid_device.name) - 1] = '\0';
    }
}

/**
 * @brief Route a host output command report to the gamepad.
 *
 * Haptic/player-LED commands are relayed verbatim over the reliable lane.
 * A features request instead arms _si_current_command and kicks off the
 * dedicated feature request so the reply can be captured on the next poll.
 */
static void _core_sinput_output_tunnel(const uint8_t *data, uint16_t len)
{
    dongle_api_host_transport_set_outputreport(data, len);
}

/**
 * @brief Produce the next 64-byte SInput input report.
 *
 * When a features request is pending and the matching reliable reply has
 * arrived, that command/data report is surfaced once (and cached). Otherwise
 * the latest unreliable input packet from core0 is returned as the normal
 * gamepad state.
 */
static bool _core_sinput_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_SINPUT;
    out->size = 64;

    if(dongle_api_host_transport_get_inputreport(out->data, &out->size));

    return true;
}

/** @brief Stop USB and clear pending feature-command state on teardown. */
static void _core_sinput_deinit(void)
{

}

/** @brief Per-tick servicing of the USB transport. */
static void _core_sinput_task(uint64_t timestamp)
{
    if(_sinput_params->core_transport_task)
    {
        _sinput_params->core_transport_task(timestamp);
    }
}

/* Populate params with SInput callbacks/descriptors and start USB if waking. */
bool core_sinput_init(core_params_s *params, const dongle_wake_s *wake)
{
    _sinput_params = params;
    _sinput_apply_wake(wake);

    sinput_hid_get_descriptor_params(
        &_sinput_hid_device.hid_report_descriptor, &_sinput_hid_device.hid_report_descriptor_len,
        &_sinput_hid_device.config_descriptor, &_sinput_hid_device.config_descriptor_len, 
        NULL, NULL
    );

    _sinput_hid_device.device_descriptor = (hoja_usb_device_descriptor_t*) sinput_hid_get_device_descriptor();

    params->core_pollrate_us = 1000;
    params->hid_device = &_sinput_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_SINPUT;
    params->core_report_generator = _core_sinput_get_generated_report;
    params->core_output_report_tunnel = _core_sinput_output_tunnel;
    params->core_deinit = _core_sinput_deinit;
    params->core_task = _core_sinput_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    return transport_init(params);
}
