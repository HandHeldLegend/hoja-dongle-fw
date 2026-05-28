#include <string.h>

#include <hoja_usb.h>
#include <dongle.h>

#include "ns_lib_hid.h"
#include "cores/core_switch.h"
#include "cores/core_usb.h"
#include "cores/cores.h"
#include "dongle_wlan.h"
#include "transport/transport.h"
#include "utilities/crosscore_snapshot.h"

#define SWPRO_INPUT_LEN 64

typedef struct
{
    uint8_t report[SWPRO_INPUT_LEN];
} core_switch_report_s;

SNAPSHOT_TYPE(switch_report, core_switch_report_s);
static snapshot_switch_report_t _snap_switch;

static hoja_usb_device_descriptor_t _switch_device_descriptor;
static core_hid_device_t _switch_hid_device;
static const uint8_t *_switch_hid_report_descriptor;
static uint16_t _switch_hid_report_descriptor_len;
static const uint8_t *_switch_configuration_descriptor;
static uint16_t _switch_configuration_descriptor_len;

static core_usb_state_t _switch_usb;
static core_params_s *_switch_params;
static bool _switch_descriptors_loaded;

static bool _switch_load_descriptors(void)
{
    if (_switch_descriptors_loaded)
    {
        return true;
    }

    uint16_t vid = 0;
    uint16_t pid = 0;
    if (!ns_hid_get_descriptor_params(&_switch_hid_report_descriptor, &_switch_hid_report_descriptor_len,
                                      &_switch_configuration_descriptor, &_switch_configuration_descriptor_len,
                                      &vid, &pid))
    {
        return false;
    }

    const ns_usb_device_descriptor_t *ns_dev = ns_hid_get_device_descriptor();
    if (!ns_dev)
    {
        return false;
    }

    _switch_device_descriptor.bLength = sizeof(hoja_usb_device_descriptor_t);
    _switch_device_descriptor.bDescriptorType = HUSB_DESC_DEVICE;
    _switch_device_descriptor.bcdUSB = ns_dev->bcdUSB;
    _switch_device_descriptor.bDeviceClass = ns_dev->bDeviceClass;
    _switch_device_descriptor.bDeviceSubClass = ns_dev->bDeviceSubClass;
    _switch_device_descriptor.bDeviceProtocol = ns_dev->bDeviceProtocol;
    _switch_device_descriptor.bMaxPacketSize0 = ns_dev->bMaxPacketSize0;
    _switch_device_descriptor.idVendor = ns_dev->idVendor;
    _switch_device_descriptor.idProduct = ns_dev->idProduct;
    _switch_device_descriptor.bcdDevice = ns_dev->bcdDevice;
    _switch_device_descriptor.iManufacturer = ns_dev->iManufacturer;
    _switch_device_descriptor.iProduct = ns_dev->iProduct;
    _switch_device_descriptor.iSerialNumber = ns_dev->iSerialNumber;
    _switch_device_descriptor.bNumConfigurations = ns_dev->bNumConfigurations;

    _switch_hid_device.config_descriptor = _switch_configuration_descriptor;
    _switch_hid_device.config_descriptor_len = _switch_configuration_descriptor_len;
    _switch_hid_device.hid_report_descriptor = _switch_hid_report_descriptor;
    _switch_hid_device.hid_report_descriptor_len = _switch_hid_report_descriptor_len;
    _switch_hid_device.device_descriptor = &_switch_device_descriptor;
    _switch_hid_device.vid = vid;
    _switch_hid_device.pid = pid;

    _switch_descriptors_loaded = true;
    return true;
}

static void _switch_apply_wake(const dongle_wake_s *wake, core_hid_device_t *hid)
{
    if (wake->vid)
    {
        hid->vid = wake->vid;
        _switch_device_descriptor.idVendor = wake->vid;
    }
    if (wake->pid)
    {
        hid->pid = wake->pid;
        _switch_device_descriptor.idProduct = wake->pid;
    }
}

static void _switch_input_tunnel(const uint8_t *data, uint16_t len)
{
    if (len != SWPRO_INPUT_LEN)
    {
        return;
    }
    snapshot_switch_report_write(&_snap_switch, (core_switch_report_s *)data);
}

static bool _switch_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_SWPRO;
    out->size = SWPRO_INPUT_LEN;
    snapshot_switch_report_read(&_snap_switch, (core_switch_report_s *)out->data);
    return true;
}

static void _switch_output_tunnel(const uint8_t *data, uint16_t len)
{
    dongle_wlan_queue_output(data, len);
}

static void _switch_deinit(void)
{
    core_usb_stop(&_switch_usb);
}

static void _switch_task(uint64_t timestamp)
{
    core_usb_task(&_switch_usb, timestamp);
}

bool core_switch_init(core_params_s *params, const dongle_wake_s *wake)
{
    if (!_switch_load_descriptors())
    {
        return false;
    }

    _switch_params = params;
    _switch_usb = (core_usb_state_t){.params = params, .transport_active = false};

    params->core_pollrate_us = 8000;
    params->hid_device = &_switch_hid_device;
    params->core_report_format = CORE_REPORTFORMAT_SWPRO;
    params->core_report_generator = _switch_get_generated_report;
    params->core_input_report_tunnel = _switch_input_tunnel;
    params->core_output_report_tunnel = _switch_output_tunnel;
    params->core_deinit = _switch_deinit;
    params->core_task = _switch_task;
    params->core_transport = GAMEPAD_TRANSPORT_USB;

    if (!wake)
    {
        return true;
    }

    return core_usb_start(&_switch_usb, wake, _switch_apply_wake);
}
