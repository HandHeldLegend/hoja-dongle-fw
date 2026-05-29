#include "cores/core_gamecube.h"

#include <string.h>

#include "dongle_wlan.h"
#include "transport/transport.h"

static core_gamecube_report_s _last_report;

bool _core_gamecube_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_GAMECUBE;
    out->size = sizeof(core_gamecube_report_s);

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

core_params_s *_gamecube_core_params = NULL;

void _core_gamecube_task(uint64_t timestamp)
{
    if (_gamecube_core_params->core_transport_task)
    {
        _gamecube_core_params->core_transport_task(timestamp);
    }
}

bool core_gamecube_init(core_params_s *params)
{
    _gamecube_core_params = params;

    params->core_pollrate_us = 1000;

    params->core_report_format = CORE_REPORTFORMAT_GAMECUBE;
    params->core_report_generator = _core_gamecube_get_generated_report;
    params->core_input_report_tunnel = NULL;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_gamecube_task;

    params->core_transport = GAMEPAD_TRANSPORT_JOYBUSGC;

    return transport_init(params);
}
