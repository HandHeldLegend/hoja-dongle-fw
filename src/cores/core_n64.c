#include <stdlib.h>
#include <hoja_types.h>

#include "cores/core_n64.h"
#include "transport/transport.h"

#include "utilities/crosscore_snapshot.h"

SNAPSHOT_TYPE(n64_report, core_n64_report_s);
snapshot_n64_report_t _snap_n64;

void _core_n64_input_tunnel(const uint8_t *data, uint16_t len)
{
    if(len!=sizeof(core_n64_report_s)) return;
    snapshot_n64_report_write(&_snap_n64, (core_n64_report_s*)data);
}

// Callback function for our transport to obtain the latest report
bool _core_n64_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_N64;
    out->size = sizeof(core_n64_report_s);

    snapshot_n64_report_read(&_snap_n64, (core_n64_report_s*)out->data);
    return true;
}

core_params_s *_n64_core_params = NULL;

void _core_n64_task(uint64_t timestamp)
{
    if(_n64_core_params->core_transport_task)
    {
        _n64_core_params->core_transport_task(timestamp);
    }
}

/*------------------------------------------------*/

// Public Functions
bool core_n64_init(core_params_s *params)
{
    _n64_core_params = params;

    params->core_pollrate_us = 1000;
    
    params->core_report_format      = CORE_REPORTFORMAT_N64;
    params->core_report_generator   = _core_n64_get_generated_report;
    params->core_input_report_tunnel = _core_n64_input_tunnel;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_n64_task;

    params->core_transport = GAMEPAD_TRANSPORT_JOYBUS64;

    return transport_init(params);
}
