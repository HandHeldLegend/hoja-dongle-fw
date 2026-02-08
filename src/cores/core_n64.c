#include <stdlib.h>
#include <hoja_types.h>

#include "cores/core_n64.h"
#include "transport/transport.h"

#define CORE_N64_CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

// Callback function for our transport to obtain the latest report
bool _core_n64_get_generated_report(core_report_s *out)
{

    return true;
}

/*------------------------------------------------*/

// Public Functions
bool core_n64_init(core_params_s *params)
{
    switch(params->transport_type)
    {
        // Supported transport methods
        case GAMEPAD_TRANSPORT_JOYBUS64:
        params->core_pollrate_us = 1000;
        break;

        case GAMEPAD_TRANSPORT_WLAN:
        params->core_pollrate_us = 2000;
        break;

        // Unsupported transport methods
        default:
        return false;
    }
    
    params->core_report_format      = CORE_REPORTFORMAT_N64;
    params->core_report_generator   = _core_n64_get_generated_report;
    params->core_report_tunnel      = NULL;

    return transport_init(params);
}
