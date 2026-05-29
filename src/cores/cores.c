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

static const dongle_wake_s _boot_wake = {
    .mode = DONGLE_MODE_N64,
};

const dongle_wake_s *core_boot_wake(void)
{
    return &_boot_wake;
}

bool core_init(const dongle_wake_s *wake)
{
    if (!wake)
    {
        return false;
    }

    switch ((dongle_mode_t)wake->mode)
    {
    case DONGLE_MODE_SINPUT:
        return core_sinput_init(&_core_params, wake);

    case DONGLE_MODE_XINPUT:
        return core_xinput_init(&_core_params, wake);

    case DONGLE_MODE_SWITCH:
        return core_switch_init(&_core_params, wake);

    case DONGLE_MODE_N64:
        return core_n64_init(&_core_params);

    case DONGLE_MODE_GAMECUBE:
        return core_gamecube_init(&_core_params);

    case DONGLE_MODE_SLIPPI:
        return core_slippi_init(&_core_params, wake);

    case DONGLE_MODE_SNES:
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
