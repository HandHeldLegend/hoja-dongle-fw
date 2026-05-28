#ifndef CORE_USB_H
#define CORE_USB_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>
#include "cores/cores.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    core_params_s *params;
    bool transport_active;
} core_usb_state_t;

typedef void (*core_usb_apply_wake_t)(const dongle_wake_s *wake, core_hid_device_t *hid);

void core_usb_task(core_usb_state_t *state, uint64_t timestamp);
bool core_usb_start(core_usb_state_t *state, const dongle_wake_s *wake, core_usb_apply_wake_t apply_wake);
void core_usb_stop(core_usb_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
