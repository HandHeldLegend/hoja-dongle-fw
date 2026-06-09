/*
 * USB hardware abstraction layer for the dongle USB transport.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file usb_hal.c
 * @brief HHL-TINYUSB transport glue for the dongle.
 *
 * Adapts the active gamepad core to the shared HHL-TINYUSB-DRIVERS stack.
 * SOF pacing drives the wireless link pump; mount/unmount hooks publish
 * transport status to core0.
 */

#include <string.h>

#include "transport/transport_usb.h"

#include "cores/cores.h"

#include <dongle_host.h>

#include "hhl_tusb.h"
#include "tusb.h"

#if !defined(HOJA_MANUFACTURER)
#define USB_MANUFACTURER "HHL"
#else
#define USB_MANUFACTURER HOJA_MANUFACTURER
#endif

#if !defined(HOJA_PRODUCT)
#define USB_PRODUCT "Dongle"
#else
#define USB_PRODUCT HOJA_PRODUCT
#endif

core_params_s *_usb_core_params = NULL;
const core_hid_device_t *_usbhal_hiddev = NULL;

static char _usb_product_str[64];
static char _usb_mfg_str[DONGLE_WAKE_MANUFACTURER_LEN];
static const char _usb_serial_str[] = "000000";

volatile uint8_t ms_counter = 0;
uint32_t _usb_frames = 8;
volatile bool _usb_ready = false;
volatile bool _usb_sendit = false;

static void _usb_hal_hid_output_report(const uint8_t *buffer, uint16_t len)
{
    if (_usb_core_params != NULL && _usb_core_params->core_output_report_tunnel != NULL)
    {
        _usb_core_params->core_output_report_tunnel(buffer, len);
    }
}

static void _usb_hal_on_mount(void)
{
    dongle_api_host_transport_set_transport(true);
}

static void _usb_hal_on_sof(uint32_t frame_count)
{
    (void)frame_count;

    ms_counter++;

    if (ms_counter >= _usb_frames)
    {
        ms_counter = 0;
    }
    else if (ms_counter >= _usb_frames - 1)
    {
        dongle_api_host_transport_mark_sent();
        _usb_sendit = true;
    }
}

static void _usb_hal_configure_strings(void)
{
    const char *mfg = USB_MANUFACTURER;
    const char *product = USB_PRODUCT;

    if (_usbhal_hiddev != NULL)
    {
        if (_usbhal_hiddev->manufacturer[0] != '\0')
        {
            mfg = _usbhal_hiddev->manufacturer;
        }
        if (_usbhal_hiddev->name[0] != '\0')
        {
            product = _usbhal_hiddev->name;
        }
    }

    strncpy(_usb_mfg_str, mfg, sizeof(_usb_mfg_str) - 1);
    _usb_mfg_str[sizeof(_usb_mfg_str) - 1] = '\0';

    strncpy(_usb_product_str, product, sizeof(_usb_product_str) - 1);
    _usb_product_str[sizeof(_usb_product_str) - 1] = '\0';
}

void transport_usb_stop()
{
    ms_counter = 0;
    dongle_api_host_transport_set_transport(false);
    hhl_tusb_stop();
}

core_report_s _core_report = {0};

bool transport_usb_init(core_params_s *params)
{
    _usb_core_params = params;
    ms_counter = 0;
    memset(_core_report.data, 0, 64);

    if (_usb_core_params->hid_device)
    {
        _usbhal_hiddev = _usb_core_params->hid_device;
    }
    else
    {
        return false;
    }

    _usb_hal_configure_strings();

    hhl_tusb_config_s tcfg = {0};
    tcfg.strings.manufacturer = _usb_mfg_str;
    tcfg.strings.product = _usb_product_str;
    tcfg.strings.serial_number = _usb_serial_str;

    tcfg.hooks.hid_output_report = _usb_hal_hid_output_report;
    tcfg.hooks.on_mount = _usb_hal_on_mount;
    tcfg.hooks.on_sof = _usb_hal_on_sof;

    switch (_usb_core_params->core_report_format)
    {
    case CORE_REPORTFORMAT_SINPUT:
        _usb_frames = 2;
        tcfg.driver = HHL_TUSB_DRIVER_HID;
        tcfg.webusb.enabled = true;
        break;

    case CORE_REPORTFORMAT_SWPRO:
        _usb_frames = 8;
        tcfg.driver = HHL_TUSB_DRIVER_HID;
        tcfg.webusb.enabled = true;
        break;

    case CORE_REPORTFORMAT_XINPUT:
#if (HHL_TUSB_DRIVER_XINPUT_ENABLE)
        _usb_frames = 2;
        tcfg.driver = HHL_TUSB_DRIVER_XINPUT;
        break;
#else
        return false;
#endif

    case CORE_REPORTFORMAT_SLIPPI:
#if (HHL_TUSB_DRIVER_SLIPPI_ENABLE)
        _usb_frames = 2;
        tcfg.driver = HHL_TUSB_DRIVER_SLIPPI;
        break;
#else
        return false;
#endif

    default:
        return false;
    }

    if (tcfg.driver == HHL_TUSB_DRIVER_HID)
    {
        tcfg.hid.device_descriptor = (const uint8_t *)_usbhal_hiddev->device_descriptor;
        tcfg.hid.device_descriptor_len = HHL_TUSB_STD_DEVICE_DESC_LEN;
        tcfg.hid.config_descriptor = _usbhal_hiddev->config_descriptor;
        tcfg.hid.config_descriptor_len = _usbhal_hiddev->config_descriptor_len;
        tcfg.hid.report_descriptor = _usbhal_hiddev->hid_report_descriptor;
        tcfg.hid.report_descriptor_len = _usbhal_hiddev->hid_report_descriptor_len;
        tcfg.hid.max_power_ma = _usbhal_hiddev->max_power_ma;
    }

    hhl_tusb_init(&tcfg);

    return hhl_tusb_start();
}

void transport_usb_task(uint64_t timestamp)
{
    (void)timestamp;

    hhl_tusb_task();

    if (!_usb_ready)
    {
        _usb_ready = hhl_tusb_report_ready();
    }

    if (_usb_sendit && _usb_ready)
    {
        _usb_sendit = false;
        _usb_ready = false;

        if (core_get_generated_report(&_core_report))
        {
            if (hhl_tusb_report_send(_core_report.data[0], &_core_report.data[1], _core_report.size - 1))
            {
                hhl_tusb_task();
            }
        }
    }
}

void tud_umount_cb(void)
{
    dongle_api_host_transport_set_transport(false);
}
