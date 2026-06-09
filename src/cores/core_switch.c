/*
 * Nintendo Switch Pro Controller core: USB personality and report bridging.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_switch.c
 * @brief Nintendo Switch Pro Controller USB personality.
 *
 * Presents the dongle as a Switch Pro Controller. The HID descriptors and
 * device identity are sourced at runtime from the ns_lib_hid library rather
 * than hard-coded here. Input reports flow from core0's unreliable lane to the
 * host, and host output reports (rumble, subcommands, etc.) are relayed back to
 * the gamepad over core0's reliable lane.
 */

#include <string.h>

#include <dongle.h>

#include "ns_lib_hid.h"
#include "ns_lib_config.h"
#include "ns_lib_types.h"

#include "cores/core_switch.h"
#include "cores/cores.h"
#include <dongle_host.h>
#include "transport/transport.h"

#define CORE_SWITCH_DEFAULT_MFG "Nintendo"

static core_hid_device_t _core_switch_hid_device_usb = {0};
static hoja_usb_device_descriptor_t _core_switch_device_descriptor = {0};

static bool _core_switch_populate_hid_device(void)
{
    ns_device_config_s ns_cfg = {0};
    ns_cfg.type = NS_DEVTYPE_PROCON;
    ns_cfg.transport = NS_TRANSPORT_USB;

    if (ns_config_set(&ns_cfg) != NS_CONFIG_OK)
    {
        return false;
    }

    const uint8_t *report_desc = NULL;
    const uint8_t *config_desc = NULL;
    uint16_t report_len = 0;
    uint16_t config_len = 0;
    uint16_t vid = 0;
    uint16_t pid = 0;

    if (!ns_hid_get_descriptor_params(&report_desc, &report_len, &config_desc, &config_len, &vid, &pid))
    {
        return false;
    }

    const ns_usb_device_descriptor_t *lib_dev = ns_hid_get_device_descriptor();
    memcpy(&_core_switch_device_descriptor, lib_dev, sizeof(_core_switch_device_descriptor));

    _core_switch_hid_device_usb.device_descriptor = &_core_switch_device_descriptor;
    _core_switch_hid_device_usb.hid_report_descriptor = report_desc;
    _core_switch_hid_device_usb.hid_report_descriptor_len = report_len;
    _core_switch_hid_device_usb.config_descriptor = config_desc;
    _core_switch_hid_device_usb.config_descriptor_len = config_len;
    _core_switch_hid_device_usb.vid = vid;
    _core_switch_hid_device_usb.pid = pid;

    const char *dev_name = ns_hid_get_device_name();
    if (dev_name != NULL)
    {
        strncpy(_core_switch_hid_device_usb.name, dev_name, sizeof(_core_switch_hid_device_usb.name) - 1);
        _core_switch_hid_device_usb.name[sizeof(_core_switch_hid_device_usb.name) - 1] = '\0';
    }

    /* NS-LIB-HID encodes 500 mA; preserve the 250 mA the dongle historically advertised. */
    _core_switch_hid_device_usb.max_power_ma = 250;

    return true;
}

/**
 * @brief Produce the next 64-byte Switch Pro input report.
 *
 * Uses the freshest correctly-sized unreliable packet from core0 when present,
 * otherwise repeats the last known report to avoid stalled/garbage frames.
 */
static bool _switch_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_SWPRO;
    out->size = 64;

    if (dongle_api_host_transport_get_inputreport(out->data, &out->size))
    {
        return true;
    }

    return false;
}

/** @brief Relay a host output report (rumble/subcommand) to the gamepad reliably. */
static void _switch_output_report_tunnel(const uint8_t *data, uint16_t len)
{
    dongle_api_host_transport_set_outputreport(data, len);
}

static const core_params_s *_switch_params = NULL;

/** @brief Stop the USB transport when the core is torn down. */
static void _switch_deinit(void)
{
}

/** @brief Per-tick servicing of the USB transport. */
static void _switch_task(uint64_t timestamp)
{
    if (_switch_params->core_transport_task)
    {
        _switch_params->core_transport_task(timestamp);
    }
}

/* Load descriptors, populate params with Switch callbacks, start USB if waking. */
bool core_switch_init(core_params_s *params, const dongle_wake_s *wake)
{
    _switch_params = params;

    if (!_core_switch_populate_hid_device())
    {
        return false;
    }

    core_hid_apply_wake_identity(wake, &_core_switch_hid_device_usb,
                                 _core_switch_hid_device_usb.name, CORE_SWITCH_DEFAULT_MFG);
    core_hid_sync_device_descriptor(&_core_switch_hid_device_usb, &_core_switch_device_descriptor);

    params->core_pollrate_us = 8000;
    params->hid_device = &_core_switch_hid_device_usb;
    params->core_report_format = CORE_REPORTFORMAT_SWPRO;
    params->core_report_generator = _switch_get_generated_report;
    params->core_output_report_tunnel = _switch_output_report_tunnel;
    params->core_deinit = _switch_deinit;
    params->core_task = _switch_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    return transport_init(params);
}
