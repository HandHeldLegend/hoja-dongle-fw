#include "cores/core_gamecube.h"
#include "transport/transport.h"

void _core_gamecube_report_tunnel_cb(const uint8_t *data, uint16_t len)
{
    // Unused
}

bool _core_gamecube_get_generated_report(core_report_s *out)
{
    

    return true;
}

/*------------------------------------------------*/

// Public Functions
bool core_gamecube_init(core_params_s *params)
{
    switch(params->transport_type)
    {
        case GAMEPAD_TRANSPORT_JOYBUSGC:
        params->core_pollrate_us = 1000;
        break;

        case GAMEPAD_TRANSPORT_WLAN:
        params->core_pollrate_us = 2000;
        break;

        // Unsupported transport methods
        default:
        return false;
    }
    
    params->core_report_format       = CORE_REPORTFORMAT_GAMECUBE;
    params->core_report_generator    = _core_gamecube_get_generated_report;
    params->core_report_tunnel       = _core_gamecube_report_tunnel_cb;

    return transport_init(params);
}