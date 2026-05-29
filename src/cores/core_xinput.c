#include <string.h>

#include <hoja_usb.h>
#include <dongle.h>

#include "cores/core_xinput.h"
#include "cores/core_usb.h"
#include "cores/cores.h"
#include "dongle_wlan.h"
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

static core_xinput_report_s _last_report;

static bool _xinput_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_XINPUT;
    out->size = XINPUT_REPORT_LEN;

    uint16_t len = 0;
    if (dongle_wlan_read_next(out->data, &len) && len == out->size)
    {
        memcpy(&_last_report, out->data, len);
    }
    else
    {
        memcpy(out->data, &_last_report, out->size);
    }
    return true;
}

static void _xinput_output_tunnel(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

static void _xinput_deinit(void)
{
    core_usb_stop(&_xinput_usb);
}

static void _xinput_task(uint64_t timestamp)
{
    core_usb_task(&_xinput_usb, timestamp);
}

bool core_xinput_init(core_params_s *params, const dongle_wake_s *wake)
{
    _xinput_params = params;
    _xinput_usb = (core_usb_state_t){.params = params, .transport_active = false};

    params->core_pollrate_us = 1000;
    params->hid_device = &_xinput_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_XINPUT;
    params->core_report_generator = _xinput_get_generated_report;
    params->core_input_report_tunnel = NULL;
    params->core_output_report_tunnel = _xinput_output_tunnel;
    params->core_deinit = _xinput_deinit;
    params->core_task = _xinput_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    if (!wake)
    {
        return true;
    }

    return core_usb_start(&_xinput_usb, wake, _xinput_apply_wake);
}
