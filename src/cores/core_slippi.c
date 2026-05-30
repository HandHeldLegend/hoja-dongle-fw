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
 * Emulates the Nintendo "WUP-028" GameCube controller adapter (VID 0x057E,
 * PID 0x0337) so PCs running Slippi/Dolphin see a real adapter. The 37-byte
 * input report mimics the adapter's multi-port framing (port status bytes plus
 * per-port controller data); only port 1 carries live gamepad data sourced from
 * core0's unreliable lane. Host output reports drive rumble and adapter init.
 */

#include <hoja_usb.h>
#include <dongle.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "cores/core_slippi.h"

#include "cores/cores.h"
#include "cores/core_usb.h"
#include "core0transport.h"
#include "transport/transport.h"

#include "hardware/watchdog.h"

typedef struct
{
    uint8_t report[37];
} slippi_report_s;

/**** GameCube Adapter HID Report Descriptor ****/
const uint8_t _gc_hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x11,        //     Report ID (17)
    0x19, 0x00,        //     Usage Minimum (Undefined)
    0x2A, 0xFF, 0x00,  //     Usage Maximum (0xFF)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x05,        //     Report Count (5)
    0x91, 0x00,        //     Output (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,              //   End Collection
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x21,        //     Report ID (33)
    0x05, 0x00,        //     Usage Page (Undefined)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0xFF,        //     Logical Maximum (-1)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x08,        //     Usage Maximum (0x08)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x32,        //     Usage (Z)
    0x09, 0x33,        //     Usage (Rx)
    0x09, 0x34,        //     Usage (Ry)
    0x09, 0x35,        //     Usage (Rz)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x06,        //     Report Count (6)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x13,        //     Report ID (19)
    0x19, 0x00,        //     Usage Minimum (Undefined)
    0x2A, 0xFF, 0x00,  //     Usage Maximum (0xFF)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x00,        //     Output (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

/**** GameCube Adapter Device Descriptor ****/
const hoja_usb_device_descriptor_t _slippi_device_descriptor = {
    .bLength = sizeof(hoja_usb_device_descriptor_t),
    .bDescriptorType = HUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = 64,
    .idVendor = 0x057E,
    .idProduct = 0x0337,

    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

/**** GameCube Adapter Configuration Descriptor ****/
#define SLIPPI_CONFIG_DESCRIPTOR_LEN 41
const uint8_t _slippi_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    HUSB_CONFIG_DESCRIPTOR(1, 1, 0, SLIPPI_CONFIG_DESCRIPTOR_LEN, HUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),

    // Interface
    9, HUSB_DESC_INTERFACE, 0x00, 0x00, 0x02, HUSB_CLASS_HID, 0x00, 0x00, 0x00,
    // HID Descriptor
    9, HHID_DESC_TYPE_HID, HUSB_U16_TO_U8S_LE(0x0110), 0, 1, HHID_DESC_TYPE_REPORT, HUSB_U16_TO_U8S_LE(sizeof(_gc_hid_report_descriptor)),
    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x82,
    HUSB_XFER_INTERRUPT,
    HUSB_U16_TO_U8S_LE(37),
    1,

    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x01,
    HUSB_XFER_INTERRUPT,
    HUSB_U16_TO_U8S_LE(6),
    1,
};

const core_hid_device_t _slippi_hid_device = {
    .config_descriptor      = _slippi_configuration_descriptor,
    .config_descriptor_len  = SLIPPI_CONFIG_DESCRIPTOR_LEN,
    .hid_report_descriptor  = _gc_hid_report_descriptor,
    .hid_report_descriptor_len = 106,
    .device_descriptor      = &_slippi_device_descriptor,
};

// WLAN input: core0_get_unreliable_inputreport() in get_generated_report.

/**
 * @brief Handle host output reports from the GameCube adapter protocol.
 *
 * Report 0x11 is a rumble command (one enable bit per port); report 0x13 is the
 * adapter init/handshake and needs no action here. The rumble bit is mapped to
 * full or zero strength and forwarded to the gamepad via core0.
 */
void _core_slippi_output_tunnel(const uint8_t *data, uint16_t len)
{    
    switch(data[0])
    {
        // Rumble Event
        case 0x11:
        uint8_t strength = (data[1] & 0x1) ? 255 : 0;
        uint8_t brake = 0;

        core0_set_rumble(strength, strength, 0, 0);
        break;

        // Init adapter 
        case 0x013:
        break;

        default:
        break;
    }

}

/**
 * @brief Build the 37-byte GameCube adapter input report.
 *
 * Lays out the adapter framing: report id 0x21, per-port status bytes marking
 * port 1 as a connected (wired) controller and the other ports idle. The first
 * call returns the empty/status-only frame; subsequent calls overlay live port-1
 * controller data pulled from core0's unreliable lane while the link is up.
 */
bool _core_slippi_get_generated_report(core_report_s *out)
{
    static bool _slippi_first = false;

    out->reportformat = CORE_REPORTFORMAT_SLIPPI;
    out->size = 37;

    out->data[0] = 0x21;

    /* GC adapter notes
    with only black USB plugged in
    - no controller, byte 1 is 0
    - controller plugged in to port 1, byte 1 is 0x10
    - controller plugged in port 2, byte 10 is 0x10

    with both USBs plugged in
    - no controller, byte 1 is 0x04
    - controller plugged in to port 1, byte is 0x14 */
    out->data[1]  = 0x14;
    out->data[10] = 0x04;
    out->data[19] = 0x04;
    out->data[28] = 0x04;

    // Do not populate any data on first report
    if(!_slippi_first)
    {
        _slippi_first = true;
        return true;
    }

    dongle_status_u _status;
    core0_get_status(&_status);
    if (_status.link_status == DONGLE_LINK_UP)
    {
        dongle_pkt_s pkt;
        if (core0_get_unreliable_inputreport(&pkt) && pkt.len > 0)
        {
            uint16_t n = pkt.len > out->size ? out->size : pkt.len;
            memcpy(out->data, pkt.data, n);
        }
    }
    return true;
}

static core_usb_state_t _slippi_usb;
core_params_s *_slippi_core_params = NULL;

/** @brief Stop the USB transport when the core is torn down. */
void _core_slippi_deinit(void)
{
    //core_usb_stop(&_slippi_usb);
}

/** @brief Per-tick servicing of the USB transport. */
void _core_slippi_task(uint64_t timestamp)
{
    //core_usb_task(&_slippi_usb, timestamp);
}

/* Populate params with Slippi callbacks/descriptors and start USB if waking. */
bool core_slippi_init(core_params_s *params, const dongle_wake_s *wake)
{
    _slippi_core_params = params;
    _slippi_usb = (core_usb_state_t){.params = params, .transport_active = false};

    params->core_pollrate_us = 1000;
    params->hid_device = &_slippi_hid_device;

    params->core_report_format = CORE_REPORTFORMAT_SLIPPI;
    params->core_report_generator = _core_slippi_get_generated_report;
    params->core_output_report_tunnel = _core_slippi_output_tunnel;
    params->core_deinit = _core_slippi_deinit;
    params->core_task = _core_slippi_task;

    params->core_transport = GAMEPAD_TRANSPORT_USB;

    /* Configure-only call (no wake): params are set but USB is not brought up. */
    if (!wake)
    {
        return true;
    }
    return true;

    /* No apply_wake hook: the adapter identity is fixed (real Nintendo VID/PID). */
    //return core_usb_start(&_slippi_usb, wake, NULL);
}
