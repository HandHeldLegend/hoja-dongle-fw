#include "cores/core_usb.h"

#include "transport/transport.h"

void core_usb_task(core_usb_state_t *state, uint64_t timestamp)
{
    if (!state || !state->transport_active || !state->params)
    {
        return;
    }

    if (state->params->core_transport_task)
    {
        state->params->core_transport_task(timestamp);
    }
}

bool core_usb_start(core_usb_state_t *state, const dongle_wake_s *wake, core_usb_apply_wake_t apply_wake)
{
    if (!state || !state->params || !wake)
    {
        return false;
    }

    if (state->transport_active)
    {
        return true;
    }

    if (apply_wake && state->params->hid_device)
    {
        apply_wake(wake, (core_hid_device_t *)state->params->hid_device);
    }

    state->transport_active = transport_init(state->params);
    return state->transport_active;
}

void core_usb_stop(core_usb_state_t *state)
{
    if (!state)
    {
        return;
    }
    state->transport_active = false;
}
