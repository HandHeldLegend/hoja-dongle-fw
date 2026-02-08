#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cores/cores.h"
#include "transport/transport.h"


#include "cores/core_sinput.h"
#include "cores/core_slippi.h"
#include "cores/core_n64.h"
#include "cores/core_gamecube.h"

const core_params_s _core_params_default = {
    .core_report_format = CORE_REPORTFORMAT_UNDEFINED,
    .core_pollrate_us = 8000,
    .core_report_generator = NULL,
    .core_input_report_tunnel = NULL,
    .core_output_report_tunnel = NULL,
    .core_deinit = NULL,

    .core_transport = GAMEPAD_TRANSPORT_UNDEFINED,
    .core_transport_task = NULL,

    .hid_device = NULL,
};

core_params_s _core_params = {
    .core_report_format = CORE_REPORTFORMAT_UNDEFINED,
    .core_pollrate_us = 8000,
    .core_report_generator = NULL,
    .core_input_report_tunnel = NULL,
    .core_output_report_tunnel = NULL,
    .core_deinit = NULL,

    .core_transport = GAMEPAD_TRANSPORT_UNDEFINED,
    .core_transport_task = NULL,

    .hid_device = NULL,
};

bool core_is_mac_blank(uint8_t mac[6])
{
    for(int i=0; i<6; i++)
    {
        if(mac[i]>0) return false;
    }
    
    return true;
}

bool core_get_generated_report(core_report_s *out)
{
    if(!_core_params.core_report_generator) return false;
    return _core_params.core_report_generator(out);
}

void core_input_report_tunnel(hoja_wlan_report_s *report)
{
    if(!_core_params.core_input_report_tunnel) return;
    if(report->report_format != _core_params.core_report_format) return;
    _core_params.core_input_report_tunnel(report->data, report->len);
}

core_params_s* core_current_params()
{
    return &_core_params;
}

bool core_init(core_reportformat_t format)
{
    switch(format)
    {
        case CORE_REPORTFORMAT_SINPUT:
        return core_sinput_init(&_core_params);

        case CORE_REPORTFORMAT_N64:
        return core_n64_init(&_core_params);

        case CORE_REPORTFORMAT_GAMECUBE:
        return core_gamecube_init(&_core_params);

        case CORE_REPORTFORMAT_SLIPPI:
        return core_slippi_init(&_core_params);

        default:
        return false;
    }
}

void core_deinit()
{
    transport_stop();
    _core_params = _core_params_default;
}
