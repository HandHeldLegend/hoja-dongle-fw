/*
 * Console transport selection and lifecycle dispatch.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport.c
 * @brief Transport abstraction layer: selects and tears down the active backend.
 *
 * Maps the requested gamepad_transport_t to a concrete backend (USB device,
 * Joybus N64, Joybus GameCube, optional NESBus), initializes it, and wires the
 * backend's periodic task into core_params so the core loop can service it.
 * Only one backend is active at a time; its stop callback is retained so the
 * layer can cleanly tear it down on transport_stop().
 */

#include "transport/transport.h"
#include "cores/cores.h"

#include <stdlib.h>
#include <string.h>

#include "transport/transport_usb.h"
#include "transport/transport_joybus64.h"
#include "transport/transport_joybusgc.h"

/** Teardown callback for the active backend (NULL when none / no teardown needed). */
typedef void (*transport_stop_cb_t)(void);

transport_stop_cb_t _tp_stop_cb = NULL;

void transport_stop()
{
    /* Invoke and clear the active backend's stop callback exactly once. */
    if(_tp_stop_cb)
    {
        _tp_stop_cb();
        _tp_stop_cb = NULL;
    }
}

bool transport_init(core_params_s *params)
{
    /* Pick the backend matching the requested transport; on success retain its
       stop callback and publish its periodic task into core_params. */
    switch(params->core_transport)
    {   
        case GAMEPAD_TRANSPORT_USB:
        if(transport_usb_init(params))
        {
            _tp_stop_cb = transport_usb_stop;
            params->core_transport_task = transport_usb_task;
            return true;
        }
        else return false;
        break;
        
        case GAMEPAD_TRANSPORT_JOYBUS64:
        if(transport_jb64_init(params))
        {
            _tp_stop_cb = transport_jb64_stop;
            params->core_transport_task = transport_jb64_task;
            return true;
        }
        else return false;

        case GAMEPAD_TRANSPORT_JOYBUSGC:
        if(transport_jbgc_init(params))
        {
            _tp_stop_cb = transport_jbgc_stop;
            params->core_transport_task = transport_jbgc_task;
            return true;
        }
        else return false;

        /* Optional NESBus backend, compiled in only when its driver is present. */
        #if defined(HOJA_TRANSPORT_NESBUS_DRIVER)
        case GAMEPAD_TRANSPORT_NESBUS:
        if(transport_nesbus_init(params))
        {
            _tp_stop_cb = NULL;
            params->core_transport_task = transport_nesbus_task;
            return true;
        }
        else return false;
        #endif

        default:
        return false;
    }

    /* Safety net: no backend selected/initialized. */
    return false;
}
