#include <stdlib.h>
#include <string.h>
#include <hoja_types.h>

#include "cores/core_n64.h"
#include "hdongle.h"
#include "transport/transport.h"

static core_n64_report_s _last_report;

bool _core_n64_get_generated_report(core_report_s *out)
{
    out->reportformat = CORE_REPORTFORMAT_N64;
    out->size = sizeof(core_n64_report_s);

    dongle_pkt_s pkt;
    if (hdongle_rx_unreliable_read_core0(&pkt) && pkt.len == out->size)
    {
        memcpy(&_last_report, pkt.data, pkt.len);
        memcpy(out->data, &_last_report, out->size);
    }
    else
    {
        memcpy(out->data, &_last_report, out->size);
    }
    return true;
}

core_params_s *_n64_core_params = NULL;

void _core_n64_task(uint64_t timestamp)
{
    if (_n64_core_params->core_transport_task)
    {
        _n64_core_params->core_transport_task(timestamp);
    }
}

bool core_n64_init(core_params_s *params)
{
    _n64_core_params = params;

    params->core_pollrate_us = 1000;

    params->core_report_format = CORE_REPORTFORMAT_N64;
    params->core_report_generator = _core_n64_get_generated_report;
    params->core_input_report_tunnel = NULL;
    params->core_output_report_tunnel = NULL;
    params->core_task = _core_n64_task;

    params->core_transport = GAMEPAD_TRANSPORT_JOYBUS64;

    return transport_init(params);
}
