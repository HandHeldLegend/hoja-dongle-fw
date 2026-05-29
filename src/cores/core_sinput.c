#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <dongle.h>

#include "dongle_wlan.h"
#include "cores/cores.h"
#include "cores/core_usb.h"
#include "transport/transport.h"

#define SINPUT_DEFAULT_VID 0x2E8A
#define SINPUT_DEFAULT_PID 0x10C6
#define SINPUT_NAME "SInput Gamepad"

#define REPORT_ID_SINPUT_INPUT  0x01
#define REPORT_ID_SINPUT_INPUT_CMDDAT  0x02
#define REPORT_ID_SINPUT_OUTPUT_CMDDAT 0x03

#define SINPUT_COMMAND_HAPTIC       0x01
#define SINPUT_COMMAND_FEATURES     0x02
#define SINPUT_COMMAND_PLAYERLED    0x03

#define SINPUT_REPORT_LEN_COMMAND 48
#define SINPUT_REPORT_LEN_INPUT   64

static volatile bool _sinput_features_report_got;
static uint8_t _sinput_features_report_data[SINPUT_REPORT_LEN_INPUT];
static volatile uint8_t _si_current_command;

static hoja_usb_device_descriptor_t _sinput_device_descriptor = {
    .bLength = sizeof(hoja_usb_device_descriptor_t),
    .bDescriptorType = HUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = SINPUT_DEFAULT_VID,
    .idProduct = SINPUT_DEFAULT_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t _sinput_hid_report_descriptor[139] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01, 0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x25, 0xFF, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x20, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x20, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x06, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x15, 0x00, 0x26, 0xFF, 0xFF, 0x75, 0x20, 0x95, 0x01, 0x81, 0x02,
    0x09, 0x21, 0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x06, 0x81, 0x02,
    0x09, 0x22, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x1D, 0x81, 0x02,
    0x85, 0x02, 0x09, 0x23, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x81, 0x02,
    0x85, 0x03, 0x09, 0x24, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x2F, 0x91, 0x02,
    0xC0,
};

#define SINPUT_CONFIG_DESCRIPTOR_LEN 41
static const uint8_t _sinput_configuration_descriptor[SINPUT_CONFIG_DESCRIPTOR_LEN] = {
    HUSB_CONFIG_DESCRIPTOR(1, 1, 0, SINPUT_CONFIG_DESCRIPTOR_LEN, HUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 350),
    9, HUSB_DESC_INTERFACE, 0x00, 0x00, 0x02, HUSB_CLASS_HID, 0x00, 0x00, 0x00,
    9, HHID_DESC_TYPE_HID, HUSB_U16_TO_U8S_LE(0x0111), 0, 1, HHID_DESC_TYPE_REPORT, HUSB_U16_TO_U8S_LE(sizeof(_sinput_hid_report_descriptor)),
    7, HUSB_DESC_ENDPOINT, 0x81, HUSB_XFER_INTERRUPT, HUSB_U16_TO_U8S_LE(64), 1,
    7, HUSB_DESC_ENDPOINT, 0x01, HUSB_XFER_INTERRUPT, HUSB_U16_TO_U8S_LE(64), 4,
};

static core_hid_device_t _sinput_hid_device = {
    .config_descriptor = _sinput_configuration_descriptor,
    .config_descriptor_len = SINPUT_CONFIG_DESCRIPTOR_LEN,
    .hid_report_descriptor = _sinput_hid_report_descriptor,
    .hid_report_descriptor_len = sizeof(_sinput_hid_report_descriptor),
    .device_descriptor = &_sinput_device_descriptor,
    .name = SINPUT_NAME,
    .pid = SINPUT_DEFAULT_PID,
    .vid = SINPUT_DEFAULT_VID,
};

static core_usb_state_t _sinput_usb;

static void _sinput_apply_wake(const dongle_wake_s *wake, core_hid_device_t *hid)
{
    if (wake->vid)
    {
        hid->vid = wake->vid;
        _sinput_device_descriptor.idVendor = wake->vid;
    }
    if (wake->pid)
    {
        hid->pid = wake->pid;
        _sinput_device_descriptor.idProduct = wake->pid;
    }
}

static void _core_sinput_send_featurerequest(void)
{
    const uint8_t req[] = {REPORT_ID_SINPUT_OUTPUT_CMDDAT, SINPUT_COMMAND_FEATURES};
    dongle_wlan_queue_output(req, sizeof(req));
}

static void _core_sinput_output_tunnel(const uint8_t *data, uint16_t len)
{
    if (len != SINPUT_REPORT_LEN_COMMAND || data[0] != REPORT_ID_SINPUT_OUTPUT_CMDDAT)
    {
        return;
    }

    switch (data[1])
    {
    case SINPUT_COMMAND_HAPTIC:
    case SINPUT_COMMAND_PLAYERLED:
        dongle_wlan_queue_output(data, len);
        break;

    case SINPUT_COMMAND_FEATURES:
        _si_current_command = SINPUT_COMMAND_FEATURES;
        _core_sinput_send_featurerequest();
        break;
    }
}

static bool _core_sinput_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_SINPUT;
    out->size = SINPUT_REPORT_LEN_INPUT;

    uint16_t len = 0;
    if (!dongle_wlan_read_next(out->data, &len) || len != SINPUT_REPORT_LEN_INPUT)
    {
        return true;
    }

    if (_si_current_command == SINPUT_COMMAND_FEATURES &&
        out->data[0] == REPORT_ID_SINPUT_INPUT_CMDDAT &&
        out->data[1] == SINPUT_COMMAND_FEATURES)
    {
        memcpy(_sinput_features_report_data, out->data, SINPUT_REPORT_LEN_INPUT);
        _sinput_features_report_got = true;
        _si_current_command = 0;
    }

    return true;
}

static void _core_sinput_deinit(void)
{
    core_usb_stop(&_sinput_usb);
    _sinput_features_report_got = false;
    _si_current_command = 0;
}

static void _core_sinput_task(uint64_t timestamp)
{
    core_usb_task(&_sinput_usb, timestamp);
}

bool core_sinput_init(core_params_s *params, const dongle_wake_s *wake)
{
    _sinput_usb = (core_usb_state_t){.params = params, .transport_active = false};

    params->core_pollrate_us = 1000;
    params->hid_device = &_sinput_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_SINPUT;
    params->core_report_generator = _core_sinput_get_generated_report;
    params->core_input_report_tunnel = NULL;
    params->core_output_report_tunnel = _core_sinput_output_tunnel;
    params->core_deinit = _core_sinput_deinit;
    params->core_task = _core_sinput_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    if (!wake)
    {
        return true;
    }

    return core_usb_start(&_sinput_usb, wake, _sinput_apply_wake);
}
