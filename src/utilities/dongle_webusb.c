/*
 * The config app through the dongle: WebUSB commands for the dongle itself, and a tunnel to the
 * connected gamepad for everything else.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "utilities/dongle_webusb.h"

#include <string.h>

#include <dongle.h>
#include <dongle_host.h>

#include "utilities/dongle_info.h"
#include "utilities/dongle_pin.h"
#include "utilities/host_detect.h"

#include "hhl_tusb.h"
#include "pico/bootrom.h"

#define DONGLE_WEBUSB_REPORT_LEN 64

// The dongle's own reply goes first, then the gamepad's traffic
static uint8_t  _local[DONGLE_WEBUSB_REPORT_LEN];
static uint16_t _local_len = 0;

static uint8_t  _pending[DONGLE_WEBUSB_REPORT_LEN];
static uint16_t _pending_len = 0;

static void _put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void _put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void _build_info(void)
{
    memset(_local, 0, sizeof(_local));
    _local[0] = DONGLE_WEBUSB_INFO;
    _local[1] = DONGLE_PROTOCOL_VERSION;
    _local[2] = DONGLE_BOARD_ID;
    _put_u32(&_local[4], DONGLE_FW_VERSION);

    uint8_t pin[4];
    dongle_pin_get(pin);
    _put_u16(&_local[8], dongle_wlan_pin_to_u16(pin));

    dongle_host_kind_t kind;
    dongle_mode_t mode;
    host_detect_get(&kind, &mode);
    _local[10] = (uint8_t)kind;
    _local[11] = (uint8_t)mode;

    dongle_wake_s wake;
    if (dongle_api_host_transport_get_gamepad(&wake))
    {
        dongle_session_s session;
        dongle_session_unpack(wake.session, &session);

        _local[3] = 1;
        _local[12] = (uint8_t)session.mode;
        _local[13] = wake.flags;
        _put_u32(&_local[14], wake.fw_version);
        _put_u16(&_local[18], wake.vid);
        _put_u16(&_local[20], wake.pid);
        memcpy(&_local[22], wake.name, DONGLE_WAKE_NAME_LEN);
    }
    else
    {
        _local[12] = 0xFF;
    }

    _local_len = DONGLE_WEBUSB_REPORT_LEN;
}

void dongle_webusb_rx(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0)
    {
        return;
    }

    switch (data[0])
    {
    case DONGLE_WEBUSB_INFO:
        _build_info();
        break;

    case DONGLE_WEBUSB_BOOTLOADER:
        reset_usb_boot(0, 0);
        break;

    default:
        // Only to a connected gamepad: queued commands (a settings write) must not reach one
        // that connects later
        if (dongle_api_host_transport_get_link_status() == DONGLE_LINK_UP)
        {
            dongle_api_host_transport_send_config(data, len);
        }
        break;
    }
}

void dongle_webusb_task(void)
{
    if (!hhl_tusb_webusb_active())
    {
        return;
    }

    if (_pending_len == 0)
    {
        if (_local_len)
        {
            memcpy(_pending, _local, _local_len);
            _pending_len = _local_len;
            _local_len = 0;
        }
        else if (!dongle_api_host_transport_get_config(_pending, &_pending_len))
        {
            return;
        }
    }

    // Wait at most a millisecond: this runs in the console's transport loop
    if (hhl_tusb_webusb_report_ready_blocking(1) && hhl_tusb_webusb_report_send(_pending, _pending_len))
    {
        _pending_len = 0;
    }
}
