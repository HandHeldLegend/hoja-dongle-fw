/*
 * XInput (Xbox 360) gamepad core: USB personality and report bridging.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_xinput.c
 * @brief Xbox 360 / XInput USB gamepad personality.
 *
 * USB descriptors are owned by HHL-TINYUSB-DRIVERS. This core only bridges
 * wireless input reports to the host and applies WAKE-supplied USB strings.
 */

#include <string.h>

#include <dongle.h>

#include "hhl_tusb_xinput.h"

#include "cores/core_xinput.h"

#include "cores/cores.h"
#include <dongle_host.h>
#include "transport/transport.h"

#define XINPUT_REPORT_LEN 20
#define XINPUT_DEFAULT_MFG "Microsoft"

typedef struct
{
    uint8_t report[XINPUT_REPORT_LEN];
} core_xinput_report_s;

/* Strings only — XInput descriptors come from HHL-TINYUSB-DRIVERS. */
static core_hid_device_t _xinput_hid_device = {
    .vid = HHL_TUSB_XINPUT_VID,
    .pid = HHL_TUSB_XINPUT_PID,
    .name = HHL_TUSB_XINPUT_NAME,
    .manufacturer = XINPUT_DEFAULT_MFG,
};

static core_params_s *_xinput_params;

/* Cached last report so polls still return valid data when no fresh packet arrives. */
static core_xinput_report_s _last_report;

/**
 * @brief Produce the next 20-byte XInput report for the host.
 */
static bool _xinput_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_XINPUT;
    out->size = XINPUT_REPORT_LEN;

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

/** @brief Host->device output (rumble) handler; XInput output is not bridged. */
static void _xinput_output_tunnel(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

/** @brief Stop the USB transport when the core is torn down. */
static void _xinput_deinit(void)
{
}

/** @brief Per-tick servicing of the USB transport. */
static void _xinput_task(uint64_t timestamp)
{
    if (_xinput_params->core_transport_task)
    {
        _xinput_params->core_transport_task(timestamp);
    }
}

/* Populate params with XInput callbacks and start USB. */
bool core_xinput_init(core_params_s *params, const dongle_wake_s *wake)
{
    _xinput_params = params;

    core_hid_apply_wake_identity(wake, &_xinput_hid_device, HHL_TUSB_XINPUT_NAME, XINPUT_DEFAULT_MFG);

    params->core_pollrate_us = 1000;
    params->hid_device = &_xinput_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_XINPUT;
    params->core_report_generator = _xinput_get_generated_report;
    params->core_output_report_tunnel = _xinput_output_tunnel;
    params->core_deinit = _xinput_deinit;
    params->core_task = _xinput_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    return transport_init(params);
}
