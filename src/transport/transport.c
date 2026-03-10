#include "transport/transport.h"
#include "cores/cores.h"

#include <stdlib.h>
#include <string.h>

#include "transport/transport_usb.h"
#include "transport/transport_joybus64.h"
#include "transport/transport_joybusgc.h"

typedef void (*transport_stop_cb_t)(void);

transport_stop_cb_t _tp_stop_cb = NULL;

void transport_stop()
{
    if(_tp_stop_cb)
    {
        _tp_stop_cb();
        _tp_stop_cb = NULL;
    }
}

bool transport_init(core_params_s *params)
{
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
            _tp_stop_cb = NULL;
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

    // Fallthrough
    return false;
}
