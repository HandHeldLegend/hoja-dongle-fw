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
 * Presents the dongle to the host as a Microsoft XInput controller (VID 0x045E,
 * PID 0x028E) using the vendor-specific interface and fixed 20-byte report.
 * Input reports arrive over the wireless link via core0's unreliable lane and
 * are forwarded to the host; XInput output (rumble) is currently ignored.
 */

#include <string.h>

#include <hoja_usb.h>
#include <dongle.h>

#include "cores/core_xinput.h"
#include "cores/core_usb.h"
#include "cores/cores.h"
#include "core0transport.h"
#include "transport/transport.h"

#define XINPUT_REPORT_LEN 20

typedef struct
{
    uint8_t report[XINPUT_REPORT_LEN];
} core_xinput_report_s;

static hoja_usb_device_descriptor_t _xinput_device_descriptor = {
    .bLength = sizeof(hoja_usb_device_descriptor_t),
    .bDescriptorType = HUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x045E,
    .idProduct = 0x028E,
    .bcdDevice = 0x0114,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

#define XINPUT_CONFIG_DESCRIPTOR_LEN 32
static const uint8_t _xinput_configuration_descriptor[XINPUT_CONFIG_DESCRIPTOR_LEN] = {
    9, 2, XINPUT_CONFIG_DESCRIPTOR_LEN, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xFF, 0x5D, 0x01, 0,
    7, 5, 0x81, 3, 32, 0, 4,
    7, 5, 0x02, 3, 32, 0, 8,
};

static core_hid_device_t _xinput_hid_device = {
    .config_descriptor = _xinput_configuration_descriptor,
    .config_descriptor_len = XINPUT_CONFIG_DESCRIPTOR_LEN,
    .hid_report_descriptor = NULL,
    .hid_report_descriptor_len = 0,
    .device_descriptor = &_xinput_device_descriptor,
    .vid = 0x045E,
    .pid = 0x028E,
    .name = "XInput Gamepad",
};

static core_usb_state_t _xinput_usb;
static core_params_s *_xinput_params;

/** @brief Override descriptor VID/PID from a non-zero host wake request. */
static void _xinput_apply_wake(const dongle_wake_s *wake, core_hid_device_t *hid)
{
    if (wake->vid)
    {
        hid->vid = wake->vid;
        _xinput_device_descriptor.idVendor = wake->vid;
    }
    if (wake->pid)
    {
        hid->pid = wake->pid;
        _xinput_device_descriptor.idProduct = wake->pid;
    }
}

/* Cached last report so polls still return valid data when no fresh packet arrives. */
static core_xinput_report_s _last_report;

/**
 * @brief Produce the next 20-byte XInput report for the host.
 *
 * Pulls the freshest unreliable input packet from core0 when available and of
 * the expected size; otherwise repeats the last known report so the host never
 * sees a stalled/garbage frame.
 */
static bool _xinput_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_XINPUT;
    out->size = XINPUT_REPORT_LEN;

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

/** @brief Host->device output (rumble) handler; XInput output is not bridged. */
static void _xinput_output_tunnel(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

/** @brief Stop the USB transport when the core is torn down. */
static void _xinput_deinit(void)
{
    //core_usb_stop(&_xinput_usb);
}

/** @brief Per-tick servicing of the USB transport. */
static void _xinput_task(uint64_t timestamp)
{
    //core_usb_task(&_xinput_usb, timestamp);
}

/* Populate params with XInput callbacks/descriptors and start USB if waking. */
bool core_xinput_init(core_params_s *params, const dongle_wake_s *wake)
{
    _xinput_params = params;
    _xinput_usb = (core_usb_state_t){.params = params, .transport_active = false};

    params->core_pollrate_us = 1000;
    params->hid_device = &_xinput_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_XINPUT;
    params->core_report_generator = _xinput_get_generated_report;
    params->core_output_report_tunnel = _xinput_output_tunnel;
    params->core_deinit = _xinput_deinit;
    params->core_task = _xinput_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    /* Configure-only call (no wake): params are set but USB is not brought up. */
    if (!wake)
    {
        return true;
    }

    return false;
}
