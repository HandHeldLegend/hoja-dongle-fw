#include "cores/core_gamecube.h"
#include "transport/transport.h"

#include "utilities/crosscore_snapshot.h"

SNAPSHOT_TYPE(gamecube_report, core_gamecube_report_s);
snapshot_gamecube_report_t _snap_gamecube;

// WLAN Packets INPUT from gamepad we receive are tunneled into here
void _core_gamecube_input_tunnel(const uint8_t *data, uint16_t len)
{
    if(len!=sizeof(core_gamecube_report_s)) return;
    snapshot_gamecube_report_write(&_snap_gamecube, (core_gamecube_report_s*)data);
}

bool _core_gamecube_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_GAMECUBE;
    out->size = sizeof(core_gamecube_report_s);

    snapshot_gamecube_report_read(&_snap_gamecube, (core_gamecube_report_s*)out->data);
    return true;
}

core_params_s *_gamecube_core_params = NULL;

void _core_gamecube_task(uint64_t timestamp)
{
    if(_gamecube_core_params->core_transport_task)
    {
        _gamecube_core_params->core_transport_task(timestamp);
    }
}

/*------------------------------------------------*/

// Public Functions
bool core_gamecube_init(core_params_s *params)
{
    _gamecube_core_params = params;

    params->core_pollrate_us = 1000;

    params->core_report_format          = CORE_REPORTFORMAT_GAMECUBE;
    params->core_report_generator       = _core_gamecube_get_generated_report;
    params->core_input_report_tunnel    = _core_gamecube_input_tunnel;
    params->core_output_report_tunnel   = NULL;
    params->core_task                   = _core_gamecube_task;

    // Set the target transport type
    params->core_transport = GAMEPAD_TRANSPORT_JOYBUSGC;

    return transport_init(params);
}