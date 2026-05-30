/*
 * TinyUSB hardware abstraction layer for the dongle USB transport.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file usb_hal.c
 * @brief TinyUSB device glue: descriptors, class drivers, HID I/O, and the SOF link pump.
 *
 * This layer adapts the dongle's report cores to TinyUSB. It supplies the
 * device/configuration/string/BOS descriptor callbacks (including the Windows
 * MS OS 1.0/2.0 and WebUSB plumbing), implements custom Slippi and XInput USBD
 * class drivers alongside the stock HID path, and routes inbound/outbound HID
 * reports. The Start-of-Frame callback both paces host polling and schedules the
 * core1 wireless link pump, while the transport hooks (init/stop/task) bring the
 * USB device up or down and publish connect/idle status to core0.
 */

#include <hoja_usb.h>

#include <tusb_config.h>

#include "transport/transport_usb.h"

#include "cores/cores.h"

#include "core0transport.h"
#include "core1wlan.h"
#include "pico/time.h"

#include "hardware/structs/usb.h"

#include "bsp/board.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

#if !defined(HOJA_MANUFACTURER)
#define USB_MANUFACTURER "HOJA"
#else
#define USB_MANUFACTURER HOJA_MANUFACTURER
#endif

#if !defined(HOJA_PRODUCT)
#define USB_PRODUCT "Dongle"
#else
#define USB_PRODUCT HOJA_PRODUCT
#endif

/* USB string descriptor table; indices are referenced by other descriptors and
 * by tud_descriptor_string_cb(). Entry 0 is the language ID, not a string. */
const char *global_string_descriptor[] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    USB_MANUFACTURER,     // 1: Manufacturer
    USB_PRODUCT,          // 2: Product
    "000000",             // 3: Serials, should use chip ID
    "Hoja Gamepad"        // 4 Identifier for GC Mode
};

/* Report/ready hooks selected per report format at transport_usb_init(), so the
 * generic task loop can poll readiness and push reports without knowing the
 * active class driver (HID / XInput / Slippi). */
typedef bool (*usb_hal_report_cb_t)(uint8_t report_id, void const *report, uint16_t len);
typedef bool (*usb_hal_ready_cb_t)(void);

usb_hal_report_cb_t _usb_hal_report_cb = NULL;
usb_hal_ready_cb_t _usb_hal_ready_cb = NULL;
core_params_s *_usb_core_params = NULL;        /* Active core config (report format, tunnels, HID device). */
const core_hid_device_t *_usbhal_hiddev = NULL; /* Descriptor set for the active core. */

//--------------------------------------------------------------------+
// Slippi Types Pre-Defines
//--------------------------------------------------------------------+
#pragma region SLIPPI

#define CFG_TUD_GC_TX_BUFSIZE 37
#define CFG_TUD_GC_RX_BUFSIZE 6
typedef struct
{
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;       // optional Out endpoint
    uint8_t itf_protocol; // Boot mouse or keyboard

    uint8_t protocol_mode; // Boot (0) or Report protocol (1)
    uint8_t idle_rate;     // up to application to handle idle rate
    uint16_t report_desc_len;

    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[CFG_TUD_GC_TX_BUFSIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[CFG_TUD_GC_RX_BUFSIZE];

    // TODO save hid descriptor since host can specifically request this after enumeration
    // Note: HID descriptor may be not available from application after enumeration
    tusb_hid_descriptor_hid_t const *hid_descriptor;
} slippid_interface_t;

CFG_TUSB_MEM_SECTION static slippid_interface_t _slippid_itf[1];

/*------------- Helpers -------------*/

/**
 * @brief Map a USB interface number to its Slippi interface slot index.
 * @param itf_num Interface number from a control request.
 * @return Slot index into _slippid_itf, or 0xFF if no slot matches.
 */
static inline uint8_t slippi_get_index_by_itfnum(uint8_t itf_num)
{
    for (uint8_t i = 0; i < CFG_TUD_GC; i++)
    {
        if (itf_num == _slippid_itf[i].itf_num)
            return i;
    }

    return 0xFF;
}

//--------------------------------------------------------------------+
// APPLICATION API
//--------------------------------------------------------------------+

/**
 * @brief Whether the Slippi IN endpoint of a given instance can accept a report.
 * @param instance Slippi interface instance index.
 * @return true if the device is mounted and the IN endpoint is idle (claimable).
 */
bool tud_slippi_n_ready(uint8_t instance)
{
    uint8_t const rhport = 0;
    uint8_t const ep_in = _slippid_itf[instance].ep_in;
    return tud_ready() && (ep_in != 0) && !usbd_edpt_busy(rhport, ep_in);
}

/** @brief Convenience wrapper for tud_slippi_n_ready() on instance 0. */
bool tud_slippi_ready()
{
    return tud_slippi_n_ready(0);
}

/**
 * @brief Queue a Slippi HID report on a given instance's IN endpoint.
 *
 * Claims the IN endpoint, packs report_id into byte 0 and the payload after it,
 * then starts a fixed-size transfer. The payload is always copied as a full
 * CFG_TUD_GC_TX_BUFSIZE-1 block regardless of @p len (GC/Slippi uses fixed-size
 * reports).
 *
 * @param instance  Slippi interface instance index.
 * @param report_id Report ID written as the first byte of the buffer.
 * @param report    Pointer to the report payload.
 * @param len       Unused; transfer size is fixed at CFG_TUD_GC_TX_BUFSIZE.
 * @return true if the transfer was submitted, false if the endpoint claim failed.
 */
bool tud_slippi_n_report(uint8_t instance, uint8_t report_id, void const *report, uint16_t len)
{
    uint8_t const rhport = 0;
    slippid_interface_t *p_hid = &_slippid_itf[instance];

    // claim endpoint
    TU_VERIFY(usbd_edpt_claim(rhport, p_hid->ep_in));

    p_hid->epin_buf[0] = report_id;
    memcpy(&p_hid->epin_buf[1], report, CFG_TUD_GC_TX_BUFSIZE-1);

    return usbd_edpt_xfer(rhport, p_hid->ep_in, p_hid->epin_buf, CFG_TUD_GC_TX_BUFSIZE);
}

/** @brief Convenience wrapper for tud_slippi_n_report() on instance 0. */
bool tud_slippi_report(uint8_t report_id, void const *report, uint16_t len)
{
    return tud_slippi_n_report(0, report_id, report, len);
}

/** @brief Return the boot interface protocol negotiated for a Slippi instance. */
uint8_t tud_slippi_n_interface_protocol(uint8_t instance)
{
    return _slippid_itf[instance].itf_protocol;
}

/** @brief Return the HID protocol mode (boot/report) for a Slippi instance. */
uint8_t tud_slippi_n_get_protocol(uint8_t instance)
{
    return _slippid_itf[instance].protocol_mode;
}

//--------------------------------------------------------------------+
// USBD-CLASS API
//--------------------------------------------------------------------+

/**
 * @brief TinyUSB class reset hook: clear all Slippi interface state.
 *
 * Invoked by the USBD core on bus reset / unplug for this class driver.
 */
void slippid_reset(uint8_t rhport)
{
    (void)rhport;
    tu_memclr(_slippid_itf, sizeof(_slippid_itf));
}

/** @brief TinyUSB class init hook; invoked once when the driver is registered. */
void slippid_init(void)
{
    slippid_reset(0);
}

/**
 * @brief TinyUSB class open hook: claim the interface and parse its descriptors.
 *
 * Called during enumeration for each interface. Only opens when the active core
 * is in Slippi report format; otherwise returns 0 so TinyUSB skips this driver.
 * Walks the interface -> HID -> endpoint descriptor chain, opens the endpoint
 * pair, caches the HID/report descriptor info, and primes the OUT endpoint to
 * receive.
 *
 * @param rhport   Root hub port.
 * @param desc_itf Interface descriptor at the current parse position.
 * @param max_len  Remaining length available in the configuration descriptor.
 * @return Number of descriptor bytes consumed, or 0 if this driver declines.
 */
uint16_t slippid_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
    // Do not open if we aren't in Slippi reporting mode
    if (_usb_core_params)
    {
        if (_usb_core_params->core_report_format != CORE_REPORTFORMAT_SLIPPI)
            return 0;
    }

    // len = interface + hid + n*endpoints
    uint16_t const drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) + sizeof(tusb_hid_descriptor_hid_t) +
                                        desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_ASSERT(max_len >= drv_len, 0);

    // Find available interface
    slippid_interface_t *p_hid = NULL;
    uint8_t hid_id;
    for (hid_id = 0; hid_id < CFG_TUD_GC; hid_id++)
    {
        if (_slippid_itf[hid_id].ep_in == 0)
        {
            p_hid = &_slippid_itf[hid_id];
            break;
        }
    }
    TU_ASSERT(p_hid, 0);

    uint8_t const *p_desc = (uint8_t const *)desc_itf;

    //------------- HID descriptor -------------//
    p_desc = tu_desc_next(p_desc);
    TU_ASSERT(HID_DESC_TYPE_HID == tu_desc_type(p_desc), 0);
    p_hid->hid_descriptor = (tusb_hid_descriptor_hid_t const *)p_desc;

    //------------- Endpoint Descriptor -------------//
    p_desc = tu_desc_next(p_desc);
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, desc_itf->bNumEndpoints, TUSB_XFER_INTERRUPT, &p_hid->ep_out, &p_hid->ep_in), 0);

    if (desc_itf->bInterfaceSubClass == HID_SUBCLASS_BOOT)
        p_hid->itf_protocol = desc_itf->bInterfaceProtocol;

    p_hid->protocol_mode = HID_PROTOCOL_REPORT; // Per Specs: default is report mode
    p_hid->itf_num = desc_itf->bInterfaceNumber;

    // Use offsetof to avoid pointer to the odd/misaligned address
    p_hid->report_desc_len = tu_unaligned_read16((uint8_t const *)p_hid->hid_descriptor + offsetof(tusb_hid_descriptor_hid_t, wReportLength));

    // Prepare for output endpoint
    if (p_hid->ep_out)
    {
        if (!usbd_edpt_xfer(rhport, p_hid->ep_out, p_hid->epout_buf, sizeof(p_hid->epout_buf)))
        {
            TU_LOG_FAILED();
            TU_BREAKPOINT();
        }
    }

    return drv_len;
}

/**
 * @brief TinyUSB class control-transfer hook for the Slippi interface.
 *
 * Invoked when a control transfer targets this class. Handles standard
 * GET_DESCRIPTOR (HID and report descriptors) plus the HID class requests
 * (GET/SET_REPORT, GET/SET_IDLE, GET/SET_PROTOCOL) across the setup/data/ack
 * stages.
 *
 * @param rhport  Root hub port.
 * @param stage   Control transfer stage (setup/data/ack).
 * @param request The USB control request.
 * @return true to continue/complete, false to stall an unsupported request.
 */
bool slippid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    TU_VERIFY(request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE);

    uint8_t const hid_itf = slippi_get_index_by_itfnum((uint8_t)request->wIndex);
    TU_VERIFY(hid_itf < CFG_TUD_GC);

    slippid_interface_t *p_hid = &_slippid_itf[hid_itf];

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD)
    {
        //------------- STD Request -------------//
        if (stage == CONTROL_STAGE_SETUP)
        {
            uint8_t const desc_type = tu_u16_high(request->wValue);
            // uint8_t const desc_index = tu_u16_low (request->wValue);

            if (request->bRequest == TUSB_REQ_GET_DESCRIPTOR && desc_type == HID_DESC_TYPE_HID)
            {
                TU_VERIFY(p_hid->hid_descriptor);
                TU_VERIFY(tud_control_xfer(rhport, request, (void *)(uintptr_t)p_hid->hid_descriptor, p_hid->hid_descriptor->bLength));
            }
            else if (request->bRequest == TUSB_REQ_GET_DESCRIPTOR && desc_type == HID_DESC_TYPE_REPORT)
            {
                uint8_t const *desc_report = tud_hid_descriptor_report_cb(hid_itf);
                tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_report, p_hid->report_desc_len);
            }
            else
            {
                return false; // stall unsupported request
            }
        }
    }
    else if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS)
    {
        //------------- Class Specific Request -------------//
        switch (request->bRequest)
        {
        case HID_REQ_CONTROL_GET_REPORT:
            if (stage == CONTROL_STAGE_SETUP)
            {
                uint8_t const report_type = tu_u16_high(request->wValue);
                uint8_t const report_id = tu_u16_low(request->wValue);

                uint8_t *report_buf = p_hid->epin_buf;
                uint16_t req_len = tu_min16(request->wLength, CFG_TUD_GC_TX_BUFSIZE);

                uint16_t xferlen = 0;

                // If host request a specific Report ID, add ID to as 1 byte of response
                if ((report_id != HID_REPORT_TYPE_INVALID) && (req_len > 1))
                {
                    *report_buf++ = report_id;
                    req_len--;

                    xferlen++;
                }

                xferlen += tud_hid_get_report_cb(hid_itf, report_id, (hid_report_type_t)report_type, report_buf, req_len);
                TU_ASSERT(xferlen > 0);

                tud_control_xfer(rhport, request, p_hid->epin_buf, xferlen);
            }
            break;

        case HID_REQ_CONTROL_SET_REPORT:
            if (stage == CONTROL_STAGE_SETUP)
            {
                TU_VERIFY(request->wLength <= sizeof(p_hid->epout_buf));
                tud_control_xfer(rhport, request, p_hid->epout_buf, request->wLength);
            }
            else if (stage == CONTROL_STAGE_ACK)
            {
                uint8_t const report_type = tu_u16_high(request->wValue);
                uint8_t const report_id = tu_u16_low(request->wValue);

                uint8_t const *report_buf = p_hid->epout_buf;
                uint16_t report_len = tu_min16(request->wLength, CFG_TUD_GC_RX_BUFSIZE);

                // If host request a specific Report ID, extract report ID in buffer before invoking callback
                if ((report_id != HID_REPORT_TYPE_INVALID) && (report_len > 1) && (report_id == report_buf[0]))
                {
                    report_buf++;
                    report_len--;
                }

                tud_hid_set_report_cb(hid_itf, report_id, (hid_report_type_t)report_type, report_buf, report_len);
            }
            break;

        case HID_REQ_CONTROL_SET_IDLE:
            if (stage == CONTROL_STAGE_SETUP)
            {
                p_hid->idle_rate = tu_u16_high(request->wValue);
                if (tud_hid_set_idle_cb)
                {
                    // stall request if callback return false
                    TU_VERIFY(tud_hid_set_idle_cb(hid_itf, p_hid->idle_rate));
                }

                tud_control_status(rhport, request);
            }
            break;

        case HID_REQ_CONTROL_GET_IDLE:
            if (stage == CONTROL_STAGE_SETUP)
            {
                // TODO idle rate of report
                tud_control_xfer(rhport, request, &p_hid->idle_rate, 1);
            }
            break;

        case HID_REQ_CONTROL_GET_PROTOCOL:
            if (stage == CONTROL_STAGE_SETUP)
            {
                tud_control_xfer(rhport, request, &p_hid->protocol_mode, 1);
            }
            break;

        case HID_REQ_CONTROL_SET_PROTOCOL:
            if (stage == CONTROL_STAGE_SETUP)
            {
                tud_control_status(rhport, request);
            }
            else if (stage == CONTROL_STAGE_ACK)
            {
                p_hid->protocol_mode = (uint8_t)request->wValue;
                if (tud_hid_set_protocol_cb)
                {
                    tud_hid_set_protocol_cb(hid_itf, p_hid->protocol_mode);
                }
            }
            break;

        default:
            return false; // stall unsupported request
        }
    }
    else
    {
        return false; // stall unsupported request
    }

    return true;
}

/**
 * @brief TinyUSB class transfer-complete hook for the Slippi interface.
 *
 * Invoked when an IN or OUT transfer finishes. For the IN endpoint it signals
 * report completion; for the OUT endpoint it delivers the received report and
 * re-arms the OUT endpoint for the next packet.
 *
 * @param rhport        Root hub port.
 * @param ep_addr       Endpoint address that completed.
 * @param result        Transfer result (unused).
 * @param xferred_bytes Number of bytes transferred.
 * @return Always true.
 */
bool slippid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;

    uint8_t instance = 0;
    slippid_interface_t *p_hid = _slippid_itf;

    // Identify which interface to use
    for (instance = 0; instance < CFG_TUD_GC; instance++)
    {
        p_hid = &_slippid_itf[instance];
        if ((ep_addr == p_hid->ep_out) || (ep_addr == p_hid->ep_in))
            break;
    }
    TU_ASSERT(instance < CFG_TUD_GC);

    // Sent report successfully
    if (ep_addr == p_hid->ep_in)
    {
        if (tud_hid_report_complete_cb)
        {
            tud_hid_report_complete_cb(instance, p_hid->epin_buf, (uint16_t)xferred_bytes);
        }
    }
    // Received report
    else if (ep_addr == p_hid->ep_out)
    {
        tud_hid_set_report_cb(instance, 0, HID_REPORT_TYPE_OUTPUT, p_hid->epout_buf, (uint16_t)xferred_bytes);
        TU_ASSERT(usbd_edpt_xfer(rhport, p_hid->ep_out, p_hid->epout_buf, sizeof(p_hid->epout_buf)));
    }

    return true;
}

/* USBD class driver vtable that registers the Slippi interface with TinyUSB. */
const usbd_class_driver_t tud_slippi_driver =
    {
#if CFG_TUSB_DEBUG >= 2
        .name = "slippi",
#endif
        .init = slippid_init,
        .reset = slippid_reset,
        .open = slippid_open,
        .control_xfer_cb = slippid_control_xfer_cb,
        .xfer_cb = slippid_xfer_cb,
        .sof = NULL,
};
#pragma endregion

//--------------------------------------------------------------------+
// XInput Types Pre-Defines
//--------------------------------------------------------------------+
#pragma region XINPUT

#define CFG_TUD_XINPUT_EP_BUFSIZE 64
typedef struct
{
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out; // optional Out endpoint

    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[CFG_TUD_XINPUT_EP_BUFSIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[CFG_TUD_XINPUT_EP_BUFSIZE];

    // TODO save hid descriptor since host can specifically request this after enumeration
    // Note: HID descriptor may be not available from application after enumeration
    // tusb_xinput_descriptor_hid_t const * hid_descriptor;
} xinputd_interface_t;

CFG_TUSB_MEM_SECTION static xinputd_interface_t _xinputd_itf;

/**
 * @brief TinyUSB class reset hook: clear XInput interface state.
 *
 * Invoked by the USBD core on bus reset / unplug for this class driver.
 */
void xinputd_reset(uint8_t rhport)
{
    (void)rhport;
    tu_memclr(&_xinputd_itf, sizeof(_xinputd_itf));
}

/** @brief TinyUSB class init hook; invoked once when the driver is registered. */
void xinputd_init(void)
{
    xinputd_reset(0);
}

/**
 * @brief TinyUSB class open hook: claim the XInput interface and its endpoints.
 *
 * Called during enumeration. Verifies the Microsoft XInput subclass (0x5D),
 * then iterates the interface's endpoint descriptors, opening each and caching
 * the IN/OUT endpoint addresses by direction.
 *
 * @param rhport   Root hub port.
 * @param desc_itf Interface descriptor at the current parse position.
 * @param max_len  Remaining length available in the configuration descriptor.
 * @return Number of descriptor bytes consumed.
 */
uint16_t xinputd_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
    const char *TAG = "xinputd_open";
    // Verify our descriptor is the correct class
    TU_VERIFY(0x5D == desc_itf->bInterfaceSubClass, 0);

    // len = interface + hid + n*endpoints
    uint16_t const drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) +
                                        desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t)) +
                             16;

    TU_ASSERT(max_len >= drv_len, 0);

    uint8_t const *p_desc = tu_desc_next(desc_itf);
    uint8_t total_endpoints = 0;
    while ((total_endpoints < desc_itf->bNumEndpoints) && (drv_len <= max_len))
    {
        tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *)p_desc;
        if (TUSB_DESC_ENDPOINT == tu_desc_type(desc_ep))
        {
            TU_ASSERT(usbd_edpt_open(rhport, desc_ep));

            if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN)
            {
                _xinputd_itf.ep_in = desc_ep->bEndpointAddress;
            }
            else
            {
                _xinputd_itf.ep_out = desc_ep->bEndpointAddress;
            }
            total_endpoints += 1;
        }
        p_desc = tu_desc_next(p_desc);
    }

    return drv_len;
}

/**
 * @brief TinyUSB class control-transfer hook for XInput.
 *
 * XInput has no class-specific control requests we need to service, so this
 * accepts everything (never stalls).
 *
 * @return Always true.
 */
bool xinputd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    return true;
}

/**
 * @brief TinyUSB class transfer-complete hook for XInput.
 *
 * On IN completion, signals report completion. On OUT completion, delivers the
 * received report (rumble/LED) and re-arms the OUT endpoint.
 *
 * @param rhport        Root hub port.
 * @param ep_addr       Endpoint address that completed.
 * @param result        Transfer result (unused).
 * @param xferred_bytes Number of bytes transferred.
 * @return Always true.
 */
bool xinputd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;

    uint8_t instance = 0;

    // Sent report successfully
    if (ep_addr == _xinputd_itf.ep_in)
    {
        if (tud_hid_report_complete_cb)
        {
            tud_hid_report_complete_cb(instance, _xinputd_itf.epin_buf, (uint16_t)xferred_bytes);
        }
    }
    // Received report
    else if (ep_addr == _xinputd_itf.ep_out)
    {
        tud_hid_set_report_cb(instance, 0, HID_REPORT_TYPE_INVALID, _xinputd_itf.epout_buf, (uint16_t)xferred_bytes);
        TU_ASSERT(usbd_edpt_xfer(rhport, _xinputd_itf.ep_out, _xinputd_itf.epout_buf, sizeof(_xinputd_itf.epout_buf)));
    }

    return true;
}

/**
 * @brief Manually pump the XInput OUT endpoint so host->device data is received.
 *
 * XInput has no SET_REPORT control path for rumble; instead the host sends data
 * on the interrupt OUT endpoint. This claims and re-arms that endpoint when it
 * is idle so xinputd_xfer_cb() will fire on the next OUT packet.
 */
void tud_xinput_getout(void)
{
    if (tud_ready() && (!usbd_edpt_busy(0, _xinputd_itf.ep_out)))
    {
        usbd_edpt_claim(0, _xinputd_itf.ep_out);
        usbd_edpt_xfer(0, _xinputd_itf.ep_out, _xinputd_itf.epout_buf, sizeof(_xinputd_itf.epout_buf));
        usbd_edpt_release(0, _xinputd_itf.ep_out);
    }
}

// USER API ACCESSIBLE

/**
 * @brief Queue an XInput report on the IN endpoint (instance 0).
 *
 * Wakes the host first if the bus is suspended, then claims endpoint 0x81,
 * packs report_id into byte 0 followed by a fixed-size payload, submits the
 * transfer, and finally re-arms the OUT endpoint via tud_xinput_getout().
 *
 * @param report_id Report ID written as the first byte of the buffer.
 * @param report    Pointer to the report payload.
 * @param len       Unused; transfer size is fixed at CFG_TUD_XINPUT_EP_BUFSIZE.
 * @return Result of submitting the IN transfer.
 */
bool tud_n_xinput_report(uint8_t report_id, void const *report, uint16_t len)
{
    uint8_t const rhport = 0;

    // Remote wakeup
    if (tud_suspended())
    {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        tud_remote_wakeup();
    }

    // claim endpoint
    TU_VERIFY(usbd_edpt_claim(rhport, 0x81));

    _xinputd_itf.epin_buf[0] = report_id;
    memcpy(&_xinputd_itf.epin_buf[1], report, CFG_TUD_XINPUT_EP_BUFSIZE - 1);
    bool out = usbd_edpt_xfer(rhport, _xinputd_itf.ep_in, _xinputd_itf.epin_buf, CFG_TUD_XINPUT_EP_BUFSIZE);
    usbd_edpt_release(0, _xinputd_itf.ep_in);

    tud_xinput_getout();

    return out;
}

/** @brief Convenience wrapper for tud_n_xinput_report() on instance 0. */
bool tud_xinput_report(uint8_t report_id, void const *report, uint16_t len)
{
    return tud_n_xinput_report(report_id, report, len);
}

/**
 * @brief Whether the XInput IN endpoint can accept a report.
 * @return true if mounted and the IN endpoint is open and idle (claimable).
 */
bool tud_xinput_ready(void)
{
    uint8_t const rhport = 0;
    uint8_t const ep_in = _xinputd_itf.ep_in;
    return tud_ready() && (ep_in != 0) && !usbd_edpt_busy(rhport, ep_in);
}

/* USBD class driver vtable that registers the XInput interface with TinyUSB. */
const usbd_class_driver_t tud_xinput_driver =
    {
#if CFG_TUSB_DEBUG >= 2
        .name = "XINPUT",
#endif
        .init = xinputd_init,
        .reset = xinputd_reset,
        .open = xinputd_open,
        .control_xfer_cb = xinputd_control_xfer_cb,
        .xfer_cb = xinputd_xfer_cb,
        .sof = NULL,
};

#pragma endregion

//--------------------------------------------------------------------+
// TinyUSB Driver Get Definition
//--------------------------------------------------------------------+
/**
 * @brief TinyUSB hook returning the application class driver to register.
 *
 * Invoked by TinyUSB at init to discover app-defined class drivers. Increments
 * the driver count and selects the Slippi or XInput driver based on the active
 * core's report format (XInput is the default).
 *
 * @param[in,out] driver_count Running count of app drivers; incremented here.
 * @return Pointer to the selected class driver vtable.
 */
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count += 1;

    if (_usb_core_params)
    {
        switch (_usb_core_params->core_report_format)
        {
        case CORE_REPORTFORMAT_SLIPPI:
            return &tud_slippi_driver;

        // Default to XInput
        default:
            return &tud_xinput_driver;
        }
    }
}

//--------------------------------------------------------------------+
// Windows/WebUSB Descriptor Handlers
//--------------------------------------------------------------------+
#pragma region MS_OS_DESC

/* Scratch buffer for UTF-16LE string descriptors returned to the host. */
static uint16_t _desc_str[64];

enum
{
    VENDOR_REQUEST_WEBUSB = 1,
    VENDOR_REQUEST_MICROSOFT = 2
};

#define ITF_NUM_VENDOR 1
#define GC_ITF_NUM_VENDOR 0
#define VENDOR_REQUEST_GET_MS_OS_DESCRIPTOR 7

extern uint8_t const desc_bos[];
extern uint8_t const desc_ms_os_20[];
extern uint8_t const gc_desc_ms_os_20[];

// Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
// Define the MS OS 1.0 Descriptor
static const uint8_t MS_OS_Descriptor[] = {
    0x12,                                                                               // Descriptor length (18 bytes)
    0x03,                                                                               // Descriptor type (3 = String)
    0x4D, 0x00, 0x53, 0x00, 0x46, 0x00, 0x54, 0x00, 0x31, 0x00, 0x30, 0x00, 0x30, 0x00, // Signature: "MSFT100"
    VENDOR_REQUEST_GET_MS_OS_DESCRIPTOR,                                                // Vendor Code
    0x00                                                                                // Padding
};

// Size of the uint16_t array
#define SIZE_UINT16_ARRAY (sizeof(MS_OS_Descriptor) / 2)

static uint16_t MS_OS_Descriptor_LE_UINT16[SIZE_UINT16_ARRAY];

//--------------------------------------------------------------------+
// BOS Descriptor
//--------------------------------------------------------------------+

/* Microsoft OS 2.0 registry property descriptor
Per MS requirements https://msdn.microsoft.com/en-us/library/windows/hardware/hh450799(v=vs.85).aspx
device should create DeviceInterfaceGUIDs. It can be done by driver and
in case of real PnP solution device should expose MS "Microsoft OS 2.0
registry property descriptor". Such descriptor can insert any record
into Windows registry per device/configuration/interface. In our case it
will insert "DeviceInterfaceGUIDs" multistring property.

GUID is freshly generated and should be OK to use.

https://developers.google.com/web/fundamentals/native-hardware/build-for-webusb/
(Section Microsoft OS compatibility descriptors)
*/

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define GC_BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

#define MS_OS_20_DESC_LEN 0xB2
#define GC_MS_OS_20_DESC_LEN 158

// BOS Descriptor is required for webUSB
uint8_t const desc_bos[] = {
    // total length, number of device caps
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),

    // Vendor Code, iLandingPage
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),

    // Microsoft OS 2.0 descriptor
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)};

uint8_t const gc_desc_bos[] = {
    // BOS descriptor
    0x05,       // Descriptor size (5 bytes)
    0x0F,       // Descriptor type (BOS)
    0x21, 0x00, // Length of this + subordinate descriptors
                // (33 bytes)
    0x01,       // Number of subordinate descriptors

    // Microsoft OS 2.0 Platform Capability Descriptor

    0x1C, // Descriptor size (28 bytes)
    0x10, // Descriptor type (Device Capability)
    0x05, // Capability type (Platform)
    0x00, // Reserved

    // MS OS 2.0 Platform Capability ID (D8DD60DF-4589-4CC7-9CD2-659D9E648A9F)

    0xDF, 0x60, 0xDD, 0xD8,
    0x89, 0x45,
    0xC7, 0x4C,
    0x9C, 0xD2,
    0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,

    0x00, 0x00, 0x03, 0x06,   // Windows version (8.1) (0x06030000)
    0x9E, 0x00,               // Size, MS OS 2.0 descriptor set (158 bytes)
    VENDOR_REQUEST_MICROSOFT, // Vendor-assigned bMS_VendorCode
    0x00                      // Doesn’t support alternate enumeration
};

/**
 * @brief TinyUSB hook returning the Binary Object Store (BOS) descriptor.
 *
 * Invoked when the host requests the BOS descriptor (used to advertise WebUSB /
 * MS OS 2.0 support). Returns the GC/Slippi-specific BOS in Slippi mode,
 * otherwise the default WebUSB+MS OS 2.0 BOS. Returns 0 if no core is active.
 */
uint8_t const *tud_descriptor_bos_cb(void)
{
    if(_usb_core_params)
    {
        switch(_usb_core_params->core_report_format)
        {
            case CORE_REPORTFORMAT_SLIPPI:
            return gc_desc_bos;

            default:
            return desc_bos;
        }
    }
    return 0;
}

uint8_t const desc_ms_os_20[] =
    {
        // Set header: length, type, windows version, total length
        U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

        // Configuration subset header: length, type, configuration index, reserved, configuration total length
        U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

        // Function Subset header: length, type, first interface, reserved, subset length
        U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

        // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
        U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

        // MS OS 2.0 Registry property descriptor: length, type
        U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
        U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
        'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
        'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
        U16_TO_U8S_LE(0x0050), // wPropertyDataLength
                               // bPropertyData: “{8B3E9D2E-7EEC-4994-AAE7-0C40DE84D36D}”.
        '{', 0x00, '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '9', 0x00, 'D', 0x00, '2', 0x00, 'E', 0x00, '-', 0x00,
        '7', 0x00, 'E', 0x00, 'E', 0x00, 'C', 0x00, '-', 0x00, '4', 0x00, '9', 0x00, '9', 0x00, '4', 0x00, '-', 0x00,
        'A', 0x00, 'A', 0x00, 'E', 0x00, '7', 0x00, '-', 0x00, '0', 0x00, 'C', 0x00, '4', 0x00, '0', 0x00, 'D', 0x00,
        'E', 0x00, '8', 0x00, '4', 0x00, 'D', 0x00, '3', 0x00, '6', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t const tmp[] =
    {
        U16_TO_U8S_LE(GC_MS_OS_20_DESC_LEN - 0x0A - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
        U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
        'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
        'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
        U16_TO_U8S_LE(0x0050), // wPropertyDataLength
                               // bPropertyData: “{8B3E9D2E-7EEC-4994-AAE7-0C40DE84D36D}”.
        '{', 0x00, '9', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '9', 0x00, 'D', 0x00, '2', 0x00, 'E', 0x00, '-', 0x00,
        '7', 0x00, 'E', 0x00, 'E', 0x00, 'C', 0x00, '-', 0x00, '4', 0x00, '9', 0x00, '9', 0x00, '4', 0x00, '-', 0x00,
        'A', 0x00, 'A', 0x00, 'E', 0x00, '7', 0x00, '-', 0x00, '0', 0x00, 'C', 0x00, '4', 0x00, '0', 0x00, 'D', 0x00,
        'E', 0x00, '8', 0x00, '4', 0x00, 'D', 0x00, '3', 0x00, '6', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t const gc_desc_ms_os_20[] =
    {
        0x0A, 0x00,             // Descriptor size (10 bytes)
        0x00, 0x00,             // MS OS 2.0 descriptor set header
        0x00, 0x00, 0x03, 0x06, // Windows version (8.1) (0x06030000)
        0x9E, 0x00,             // Size, MS OS 2.0 descriptor set (158 bytes)

        // Microsoft OS 2.0 compatible ID descriptor

        0x14, 0x00,                                     // Descriptor size (20 bytes)
        0x03, 0x00,                                     // MS OS 2.0 compatible ID descriptor
        0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00, // WINUSB string
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Sub-compatible ID

        // Registry property descriptor

        0x80, 0x00, // Descriptor size (130 bytes)
        0x04, 0x00, // Registry Property descriptor
        0x01, 0x00, // Strings are null-terminated Unicode
        0x28, 0x00, // Size of Property Name (40 bytes)

        // Property Name ("DeviceInterfaceGUID")

        0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00, 0x63, 0x00, 0x65, 0x00,
        0x49, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00,
        0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00, 0x55, 0x00, 0x49, 0x00,
        0x44, 0x00, 0x00, 0x00,

        0x4E, 0x00, // Size of Property Data (78 bytes)

        // Vendor-defined Property Data: {ecceff35-146c-4ff3-acd9-8f992d09acdd}

        0x7B, 0x00, 0x65, 0x00, 0x63, 0x00, 0x63, 0x00, 0x65, 0x00, 0x66, 0x00,
        0x66, 0x00, 0x33, 0x00, 0x35, 0x00, 0x2D, 0x00, 0x31, 0x00, 0x34, 0x00,
        0x36, 0x00, 0x33, 0x00, 0x2D, 0x00, 0x34, 0x00, 0x66, 0x00, 0x66, 0x00,
        0x33, 0x00, 0x2D, 0x00, 0x61, 0x00, 0x63, 0x00, 0x64, 0x00, 0x39, 0x00,
        0x2D, 0x00, 0x38, 0x00, 0x66, 0x00, 0x39, 0x00, 0x39, 0x00, 0x32, 0x00,
        0x64, 0x00, 0x30, 0x00, 0x39, 0x00, 0x61, 0x00, 0x63, 0x00, 0x64, 0x00,
        0x64, 0x00, 0x7D, 0x00, 0x00, 0x00};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");
TU_VERIFY_STATIC(sizeof(gc_desc_ms_os_20) == GC_MS_OS_20_DESC_LEN, "Incorrect size");

/**
 * @brief TinyUSB hook returning a string descriptor, built into _desc_str.
 *
 * Invoked on GET STRING DESCRIPTOR. Index 0xEE is the special Microsoft OS 1.0
 * descriptor (advertises the vendor request code used to fetch MS OS feature
 * descriptors); index 0 returns the language ID; all other indices convert the
 * matching ASCII string from global_string_descriptor[] into UTF-16LE.
 *
 * @param index String descriptor index requested by the host.
 * @param langid Requested language ID (unused).
 * @return Pointer to the UTF-16LE descriptor; valid until the next call.
 */
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    /* 0xEE: emit the MS OS 1.0 string descriptor (repacked to UTF-16LE words). */
    if (index == 0xEE)
    {
        for (int i = 0, j = 0; i < sizeof(MS_OS_Descriptor); i += 2, j++)
        {
            MS_OS_Descriptor_LE_UINT16[j] = (MS_OS_Descriptor[i + 1] << 8) | MS_OS_Descriptor[i];
        }

        memcpy(&_desc_str[0], &MS_OS_Descriptor_LE_UINT16[0], sizeof(MS_OS_Descriptor_LE_UINT16));
        return _desc_str;
    }
    else if (index == 0)
    {
        memcpy(&_desc_str[1], global_string_descriptor[0], 2);
        chr_count = 1;
    }
    else
    {

        const char *str = global_string_descriptor[index];

        // Cap at max char... WHY?
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

/**
 * @brief TinyUSB vendor class hook: data received on the vendor OUT endpoint.
 *
 * Unused here; the vendor interface exists only to carry the MS OS / WebUSB
 * control descriptors, not bulk data.
 */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize)
{
    // UNUSED
}

uint8_t MS_OS_10_CompatibleID_Descriptor[] = {
    0x28, 0x00, 0x00, 0x00,                         // DWORD (LE)	 Descriptor length (40 bytes)
    0x00, 0x01,                                     // BCD WORD (LE)	 Version ('1.0')
    0x04, 0x00,                                     // WORD (LE)	 Compatibility ID Descriptor index (0x0004)
    0x01,                                           // BYTE	 Number of sections (1)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // 7 BYTES	 Reserved
    0x00,                                           //	 BYTE	 Interface Number (Interface #0)
    0x01,                                           //	 BYTE	 Reserved
    0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00, // 8 BYTES ASCII String Compatible ID ("WINUSB\0\0")
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 8 BYTES ASCII String	 Sub-Compatible ID (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00              // 6 BYTES Reserved
};

uint8_t MS_Extended_Feature_Descriptor[] =
    {
        0x92, 0x00, 0x00, 0x00, // DWORD (LE)	 Descriptor length (146 bytes)
        0x00, 0x01,             // BCD WORD (LE) Version ('1.0')
        0x05, 0x00,             // WORD (LE)	 Extended Property Descriptor index (0x0005)
        0x01, 0x00,             // WORD          Number of sections (1)
        0x88, 0x00, 0x00, 0x00, // DWORD (LE)	 Size of the property section (136 bytes)
        0x07, 0x00, 0x00, 0x00, // DWORD (LE)	 Property data type (7 = Unicode REG_MULTI_SZ)
        0x2A, 0x00,             // WORD (LE)	 Property name length (42 bytes)
                                // NULL-terminated Unicode String (LE)	 Property Name (L"DeviceInterfaceGUIDs")
        'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
        'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
        'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0x00, 0x00,
        0x50, 0x00, 0x00, 0x00, // DWORD (LE)	 Property data length (80 bytes)

        // NULL-terminated Unicode String (LE), followed by another Unicode NULL
        // Property Name ("{6E45736A-2B1B-4078-B772-B3AF2B6FDE1C}")
        '{', 0, '6', 0, 'E', 0, '4', 0, '5', 0, '7', 0, '3', 0, '6', 0, 'A', 0, '-', 0,
        '2', 0, 'B', 0, '1', 0, 'B', 0, '-', 0, '4', 0, '0', 0, '7', 0, '8', 0, '-', 0,
        'B', 0, '7', 0, '7', 0, '2', 0, '-', 0, 'B', 0, '3', 0, 'A', 0, 'F', 0, '2', 0,
        'B', 0, '6', 0, 'F', 0, 'D', 0, 'E', 0, '1', 0, 'C', 0, '}', 0,
        0x00, 0x00, 0x00, 0x00};

/**
 * @brief TinyUSB vendor class control-transfer hook (MS OS / WebUSB requests).
 *
 * Invoked for vendor control transfers. On the setup stage it serves the
 * Microsoft OS 1.0 compatible-ID / extended-feature descriptors (wIndex 4/5),
 * the WebUSB request (unused), and the MS OS 2.0 descriptor set (wIndex 7,
 * GC/Slippi vs default variant). Only the SETUP stage carries work here.
 *
 * @param rhport  Root hub port.
 * @param stage   Control transfer stage.
 * @param request The vendor control request.
 * @return true to continue/complete, false to stall an unsupported request.
 */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    // nothing to with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    uint8_t const desc_type = tu_u16_high(request->wValue);
    uint8_t const itf = 0;

    switch (request->bmRequestType_bit.type)
    {
    case TUSB_REQ_TYPE_STANDARD:
        // Unused for vendor control transfer
        // TinyUSB hooks in and forces this for Vendor requests only

    case TUSB_REQ_TYPE_VENDOR:
        switch (request->bRequest)
        {

        // MS OS 1.0 Descriptor
        case VENDOR_REQUEST_GET_MS_OS_DESCRIPTOR:
        {
            if (request->wIndex == 4)
            {
                if (tud_control_xfer(rhport, request, MS_OS_10_CompatibleID_Descriptor, sizeof(MS_OS_10_CompatibleID_Descriptor)))
                {
                    return true;
                }

                return false;
            }
            else if (request->wIndex == 5)
            {
                // MS descriptor 1.0 stuff

                if (tud_control_xfer(rhport, request, MS_Extended_Feature_Descriptor, sizeof(MS_Extended_Feature_Descriptor)))
                {
                    return true;
                }
            }
        }
        break;

        // Web USB Descriptor
        case VENDOR_REQUEST_WEBUSB:
        {
            // UNUSED WEBUSB
            return false;
        }

        // MS OS 2.0 Descriptor
        case VENDOR_REQUEST_MICROSOFT:
        {
            if (request->wIndex == 7)
            {
                // Get Microsoft OS 2.0 compatible descriptor
                uint16_t total_len;

                if (_usb_core_params)
                {
                    /* Total length lives at byte offset 8 of the MS OS 2.0 set header. */
                    if (_usb_core_params->core_report_format == CORE_REPORTFORMAT_SLIPPI)
                    {
                        memcpy(&total_len, gc_desc_ms_os_20 + 8, 2);
                        return tud_control_xfer(rhport, request, (void *)(uintptr_t)gc_desc_ms_os_20, total_len);
                    }
                    else
                    {
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);
                        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
                    }
                }
            }
            else
            {
                return false;
            }
        }

        default:
            break;
        }
        break;

    case TUSB_REQ_TYPE_CLASS:
        printf("Vendor Request: %x", request->bRequest);

        // response with status OK
        return tud_control_status(rhport, request);
        break;

    default:
        break;
    }

    // stall unknown request
    return false;
}

#pragma endregion

//--------------------------------------------------------------------+
// TinyUSB Function Callbacks
//--------------------------------------------------------------------+
#pragma region TUSB CALLBACKS

/***********************************************/
/********* TinyUSB HID callbacks ***************/

volatile uint8_t ms_counter = 0; /* SOF (1 ms) tick counter, wraps every _usb_frames frames. */
uint32_t _usb_frames = 8;        /* Host poll period in SOF frames (set per report format). */
// Whether USB is ready for another input
volatile bool _usb_ready = false; /* IN endpoint ready to accept the next report. */
volatile bool _usb_sendit = false; /* SOF cadence reached: time to send a report from the task loop. */

/**
 * @brief TinyUSB hook returning the device descriptor.
 *
 * Invoked on GET DEVICE DESCRIPTOR. Returns the active core's device
 * descriptor, or NULL if no HID device is currently configured.
 */
uint8_t const *tud_descriptor_device_cb(void)
{
    // TO DO set connected on transport layer
    // END TO DO
    if (_usbhal_hiddev)
    {
        return (uint8_t const *)_usbhal_hiddev->device_descriptor;
    }

    return NULL;
}

/**
 * @brief TinyUSB hook returning the configuration descriptor.
 *
 * Invoked on GET CONFIGURATION DESCRIPTOR. Returns the active core's config
 * descriptor; its contents must remain valid for the whole transfer.
 *
 * @param index Configuration index (unused; single configuration).
 */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    if (_usbhal_hiddev)
    {
        return _usbhal_hiddev->config_descriptor;
    }

    return 0;
}

/**
 * @brief TinyUSB HID hook for GET_REPORT control requests.
 *
 * Reports are pushed proactively over the IN endpoint, so this returns 0 to
 * STALL host-initiated GET_REPORT requests.
 *
 * @return Always 0 (stall).
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)reqlen;
    (void)report_type;

    return 0;
}

/**
 * @brief TinyUSB HID hook returning the HID report descriptor.
 *
 * Invoked on GET HID REPORT DESCRIPTOR. Returns the active core's report
 * descriptor (if any); contents must remain valid for the whole transfer.
 */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;

    if (_usbhal_hiddev)
    {
        if(_usbhal_hiddev->hid_report_descriptor)
            return _usbhal_hiddev->hid_report_descriptor;
    }

    return 0;
}

/**
 * @brief TinyUSB HID hook invoked when an IN report transfer completes.
 *
 * Nothing to do; the task loop drives the next report based on SOF cadence.
 */
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;
    (void)report;
    (void)len;
}

/**
 * @brief TinyUSB HID hook for SET_REPORT control requests and OUT endpoint data.
 *
 * Invoked on SET_REPORT or when data arrives on the OUT endpoint
 * (report_id = 0, type = 0). For host OUTPUT reports it forwards the payload to
 * the active core's output report tunnel (e.g. rumble/feature handling).
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    if (!report_id && report_type == HID_REPORT_TYPE_OUTPUT)
    {
        if (_usb_core_params->core_output_report_tunnel)
        {
            _usb_core_params->core_output_report_tunnel(buffer, bufsize);
        }
    }
}

/**
 * @brief TinyUSB hook invoked when the device is mounted (enumerated) by a host.
 *
 * Toggles the SOF callback off/on to guarantee it is freshly enabled, then
 * publishes CONNECTED transport status to core0.
 */
void tud_mount_cb()
{
    /* Force a clean re-enable of the SOF callback for the new connection. */
    tud_sof_cb_enable(false);
    tud_sof_cb_enable(true);
    core0_set_transport_status(DONGLE_TRANSPORT_CONNECTED);
}

/**
 * @brief TinyUSB hook invoked when the device is unmounted (unplugged/reset).
 *
 * Reports IDLE transport status to core0.
 */
void tud_umount_cb()
{
    core0_set_transport_status(DONGLE_TRANSPORT_IDLE);
}

/**
 * @brief TinyUSB Start-of-Frame hook (~1 kHz) that paces TX and the link pump.
 *
 * Each 1 ms SOF advances ms_counter. _usb_frames sets the effective host poll
 * period, so a report is emitted once every _usb_frames frames. On the frame
 * that completes a poll period it flags _usb_sendit and stamps the poll time
 * for the wireless link pump; at the half-period mark it nudges the link pump
 * so wireless TX is scheduled relative to the host poll. When polling every
 * frame (_usb_frames == 1) the half-period branch can't run, so the pump is
 * scheduled here instead.
 *
 * @param frame_count_ext Extended frame counter (unused).
 */
void tud_sof_cb(uint32_t frame_count_ext)
{
    (void)frame_count_ext;

    uint64_t now_us = time_us_64();

    ms_counter++;

    /* End of a poll period: time to send a report and mark the poll instant. */
    if (ms_counter >= _usb_frames)
    {
        ms_counter = 0;
        if (_usb_frames == 1)
        {
            core1_link_pump_schedule_from_poll(now_us);
        }
        core1_link_pump_mark_sent(now_us);
        _usb_sendit = true;
    }
    /* Half-way through the poll period: schedule the link pump to interleave. */
    else if (ms_counter == (_usb_frames >> 1))
    {
        core1_link_pump_schedule_from_poll(now_us);
    }
}

#pragma endregion

/***********************************************/
/********* Transport Defines *******************/

/**
 * @brief Tear down the USB transport.
 *
 * Clears the report hook, resets link-pump timing and the SOF counter, reports
 * IDLE transport status to core0, and deinitializes the TinyUSB device stack.
 */
void transport_usb_stop()
{
    _usb_hal_report_cb = NULL;
    core1_link_pump_reset_timing();
    ms_counter = 0;
    core0_set_transport_status(DONGLE_TRANSPORT_IDLE);
    tud_deinit(0);
}
core_report_s _core_report = {0}; /* Scratch report fetched from the core each TX cycle. */

/**
 * @brief Bring up the USB transport for a given core configuration.
 *
 * Stores the core params, resets link-pump timing/counters, then selects the
 * poll period (_usb_frames) and the ready/report hooks for the core's report
 * format (HID for SINPUT/SWPRO, XInput, or Slippi). Caches the core's HID
 * descriptor set and starts the TinyUSB device stack.
 *
 * @param params Active core configuration; must provide a hid_device.
 * @return true on successful tusb_init(); false for unsupported formats or a
 *         missing HID device.
 */
bool transport_usb_init(core_params_s *params)
{
    // Copy pointer
    _usb_core_params = params;
    core1_link_pump_reset_timing();
    ms_counter = 0;
    memset(_core_report.data, 0, 64);

    switch (_usb_core_params->core_report_format)
    {
    // Supported report formats
    case CORE_REPORTFORMAT_SINPUT:
        _usb_frames = 2;
        _usb_hal_ready_cb = tud_hid_ready;
        _usb_hal_report_cb = tud_hid_report;
        break;
    case CORE_REPORTFORMAT_SWPRO:
        _usb_frames = 8;
        _usb_hal_ready_cb = tud_hid_ready;
        _usb_hal_report_cb = tud_hid_report;
        break;

    case CORE_REPORTFORMAT_XINPUT:
        _usb_frames = 2;
        _usb_hal_ready_cb = tud_xinput_ready;
        _usb_hal_report_cb = tud_xinput_report;
        break;

    case CORE_REPORTFORMAT_SLIPPI:
        _usb_frames = 2;
        _usb_hal_ready_cb = tud_slippi_ready;
        _usb_hal_report_cb = tud_slippi_report;
        break;

    // Unsupported report formats
    default:
        return false;
    }

    if (_usb_core_params->hid_device)
    {
        _usbhal_hiddev = _usb_core_params->hid_device;
    }
    // We need the USB device properties
    else
        return false;

    return tusb_init();
}

/**
 * @brief Periodic USB transport service routine.
 *
 * Runs the TinyUSB device task, refreshes the IN-endpoint ready flag, and once
 * both the SOF cadence (_usb_sendit) and readiness line up, fetches a freshly
 * generated report from the active core and pushes it via the selected report
 * hook. The data buffer carries the report ID in byte 0, so the payload starts
 * at index 1 with length size-1.
 *
 * @param timestamp Current time in microseconds (unused).
 */
void transport_usb_task(uint64_t timestamp)
{
    tud_task();

    if (!_usb_ready && _usb_hal_ready_cb)
    {
        _usb_ready = _usb_hal_ready_cb();
    }

    /* Only emit when the host poll cadence has fired and the endpoint is free. */
    if (_usb_sendit && _usb_ready)
    {
        _usb_sendit = false;
        _usb_ready = false;
    
        if(core_get_generated_report(&_core_report))
        {
            if(_usb_hal_report_cb)
            {
                _usb_hal_report_cb(_core_report.data[0], &_core_report.data[1], _core_report.size-1);
            }
        }
    }
}
