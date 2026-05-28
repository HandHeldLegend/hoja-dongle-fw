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
#include "cores/core_xinput.h"
#include "cores/core_switch.h"

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
    .core_transport = GAMEPAD_TRANSPORT_UNDEFINED,
};

bool core_transport_is_usb(core_reportformat_t format)
{
    switch (format)
    {
    case CORE_REPORTFORMAT_SINPUT:
    case CORE_REPORTFORMAT_XINPUT:
    case CORE_REPORTFORMAT_SWPRO:
    case CORE_REPORTFORMAT_SLIPPI:
        return true;
    default:
        return false;
    }
}

bool core_get_generated_report(core_report_s *out)
{
    if (!_core_params.core_report_generator)
    {
        return false;
    }
    return _core_params.core_report_generator(out);
}

void core_input_report_tunnel(const uint8_t *data, uint16_t len)
{
    if (!_core_params.core_input_report_tunnel || !data || len == 0)
    {
        return;
    }
    _core_params.core_input_report_tunnel(data, len);
}

core_params_s *core_current_params(void)
{
    return &_core_params;
}

bool core_init(core_reportformat_t format, const dongle_wake_s *wake)
{
    if (core_transport_is_usb(format) && !wake)
    {
        return false;
    }

    switch (format)
    {
    case CORE_REPORTFORMAT_SINPUT:
        return core_sinput_init(&_core_params, wake);

    case CORE_REPORTFORMAT_XINPUT:
        return core_xinput_init(&_core_params, wake);

    case CORE_REPORTFORMAT_SWPRO:
        return core_switch_init(&_core_params, wake);

    case CORE_REPORTFORMAT_N64:
        return core_n64_init(&_core_params);

    case CORE_REPORTFORMAT_GAMECUBE:
        return core_gamecube_init(&_core_params);

    case CORE_REPORTFORMAT_SLIPPI:
        return core_slippi_init(&_core_params, wake);

    default:
        return false;
    }
}

void core_task(uint64_t timestamp)
{
    if (_core_params.core_task)
    {
        _core_params.core_task(timestamp);
    }
}

void core_deinit(void)
{
    if (_core_params.core_deinit)
    {
        _core_params.core_deinit();
    }
    transport_stop();
    _core_params = _core_params_default;
}
