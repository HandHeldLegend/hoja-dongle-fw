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

#include <hoja_usb.h>
#include <dongle.h>

#include "ns_lib_hid.h"
#include "cores/core_switch.h"
#include "cores/core_usb.h"
#include "cores/cores.h"
#include "core0transport.h"
#include "transport/transport.h"

#define CORE_SWITCH_HID_NAME "Pro Controller"
static const uint16_t _core_switch_hid_pid = 0x2009;
static const uint16_t _core_switch_hid_vid = 0x057E;

static const hoja_usb_device_descriptor_t _core_switch_device_descriptor = {
    .bLength = sizeof(hoja_usb_device_descriptor_t),
    .bDescriptorType = HUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, // Changed from 0x0200 to 2.1 for BOS & WebUSB
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = 64,
    .idVendor = _core_switch_hid_vid,
    .idProduct = _core_switch_hid_pid,

    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static const uint8_t _core_switch_hid_report_descriptor_usb[203] = {
    0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
    0x15, 0x00, // Logical Minimum (0)

    0x09, 0x04, // Usage (Joystick)
    0xA1, 0x01, // Collection (Application)

    0x85, 0x30, //   Report ID (48)
    0x05, 0x01, //   Usage Page (Generic Desktop Ctrls)
    0x05, 0x09, //   Usage Page (Button)
    0x19, 0x01, //   Usage Minimum (0x01)
    0x29, 0x0A, //   Usage Maximum (0x0A)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x0A, //   Report Count (10)
    0x55, 0x00, //   Unit Exponent (0)
    0x65, 0x00, //   Unit (None)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09, //   Usage Page (Button)
    0x19, 0x0B, //   Usage Minimum (0x0B)
    0x29, 0x0E, //   Usage Maximum (0x0E)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x04, //   Report Count (4)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x02, //   Report Count (2)
    0x81, 0x03, //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x0B, 0x01, 0x00, 0x01, 0x00, //   Usage (0x010001)
    0xA1, 0x00,                   //   Collection (Physical)
    0x0B, 0x30, 0x00, 0x01, 0x00, //     Usage (0x010030)
    0x0B, 0x31, 0x00, 0x01, 0x00, //     Usage (0x010031)
    0x0B, 0x32, 0x00, 0x01, 0x00, //     Usage (0x010032)
    0x0B, 0x35, 0x00, 0x01, 0x00, //     Usage (0x010035)
    0x15, 0x00,                   //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     Logical Maximum (65534)
    0x75, 0x10,                   //     Report Size (16)
    0x95, 0x04,                   //     Report Count (4)
    0x81, 0x02,                   //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                         //   End Collection

    0x0B, 0x39, 0x00, 0x01, 0x00, //   Usage (0x010039)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x07,                   //   Logical Maximum (7)
    0x35, 0x00,                   //   Physical Minimum (0)
    0x46, 0x3B, 0x01,             //   Physical Maximum (315)
    0x65, 0x14,                   //   Unit (System: English Rotation, Length: Centimeter)
    0x75, 0x04,                   //   Report Size (4)
    0x95, 0x01,                   //   Report Count (1)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,                   //   Usage Page (Button)
    0x19, 0x0F,                   //   Usage Minimum (0x0F)
    0x29, 0x12,                   //   Usage Maximum (0x12)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x01,                   //   Logical Maximum (1)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x04,                   //   Report Count (4)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x34,                   //   Report Count (52)
    0x81, 0x03,                   //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
    0x85, 0x21,       //   Report ID (33)
    0x09, 0x01,       //   Usage (0x01)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x85, 0x81, //   Report ID (-127)
    0x09, 0x02, //   Usage (0x02)
    0x75, 0x08, //   Report Size (8)
    0x95, 0x3F, //   Report Count (63)
    0x81, 0x03, //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x85, 0x01, //   Report ID (1)
    0x09, 0x03, //   Usage (0x03)
    0x75, 0x08, //   Report Size (8)
    0x95, 0x3F, //   Report Count (63)
    0x91, 0x83, //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)

    0x85, 0x10, //   Report ID (16)
    0x09, 0x04, //   Usage (0x04)
    0x75, 0x08, //   Report Size (8)
    0x95, 0x3F, //   Report Count (63)
    0x91, 0x83, //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)

    0x85, 0x80, //   Report ID (-128)
    0x09, 0x05, //   Usage (0x05)
    0x75, 0x08, //   Report Size (8)
    0x95, 0x3F, //   Report Count (63)
    0x91, 0x83, //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)

    0x85, 0x82, //   Report ID (-126)
    0x09, 0x06, //   Usage (0x06)
    0x75, 0x08, //   Report Size (8)
    0x95, 0x3F, //   Report Count (63)
    0x91, 0x83, //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)

    0xC0, // End Collection

    // 203 bytes
};

#define CORE_SWITCH_CONFIG_DESCRIPTOR_LEN 64
const uint8_t _core_switch_config_descriptor[CORE_SWITCH_CONFIG_DESCRIPTOR_LEN] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    HUSB_CONFIG_DESCRIPTOR(1, 2, 0, CORE_SWITCH_CONFIG_DESCRIPTOR_LEN, HUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 250),

    // Interface
    9,
    HUSB_DESC_INTERFACE,
    0x00,
    0x00,
    0x02,
    HUSB_CLASS_HID,
    0x00,
    0x00,
    0x00,
    // HID Descriptor
    9,
    HHID_DESC_TYPE_HID,
    HUSB_U16_TO_U8S_LE(0x0111),
    0,
    1,
    HHID_DESC_TYPE_REPORT,
    HUSB_U16_TO_U8S_LE(sizeof(_core_switch_hid_report_descriptor_usb)),
    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x81,
    HUSB_XFER_INTERRUPT,
    HUSB_U16_TO_U8S_LE(64),
    1, // interval
    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x01,
    HUSB_XFER_INTERRUPT,
    HUSB_U16_TO_U8S_LE(64),
    1, // report interval

    // Alternate Interface for WebUSB
    // Interface
    9,
    HUSB_DESC_INTERFACE,
    0x01,
    0x00,
    0x02,
    HUSB_CLASS_VENDOR_SPECIFIC,
    0x00,
    0x00,
    0x00,
    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x82,
    HUSB_XFER_BULK,
    HUSB_U16_TO_U8S_LE(64),
    0,
    // Endpoint Descriptor
    7,
    HUSB_DESC_ENDPOINT,
    0x02,
    HUSB_XFER_BULK,
    HUSB_U16_TO_U8S_LE(64),
    0,
};

static core_hid_device_t _core_switch_hid_device_usb = {
    .name = CORE_SWITCH_HID_NAME,
    .pid = _core_switch_hid_pid,
    .vid = _core_switch_hid_vid,

    .device_descriptor = &_core_switch_device_descriptor,

    .hid_report_descriptor = _core_switch_hid_report_descriptor_usb,
    .hid_report_descriptor_len = sizeof(_core_switch_hid_report_descriptor_usb),

    .config_descriptor = _core_switch_config_descriptor,
    .config_descriptor_len = CORE_SWITCH_CONFIG_DESCRIPTOR_LEN,
};

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

    if(core0_get_inputreport(out->data, &out->size))
    {
        return true;
    }
    
    return false;
}

/** @brief Relay a host output report (rumble/subcommand) to the gamepad reliably. */
static void _switch_output_report_tunnel(const uint8_t *data, uint16_t len)
{
    core0_send_reliable_outputreport(data, len);
}

static const core_params_s *_switch_params = NULL;

/** @brief Stop the USB transport when the core is torn down. */
static void _switch_deinit(void)
{
    
}

/** @brief Per-tick servicing of the USB transport. */
static void _switch_task(uint64_t timestamp)
{
    if(_switch_params->core_transport_task)
    {
        _switch_params->core_transport_task(timestamp);
    }
}

/* Load descriptors, populate params with Switch callbacks, start USB if waking. */
bool core_switch_init(core_params_s *params, const dongle_wake_s *wake)
{
    _switch_params = params;

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
