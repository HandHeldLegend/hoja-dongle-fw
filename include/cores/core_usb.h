/*
 * Shared USB-transport helpers for USB-based gamepad cores
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_usb.h
 * @brief Common lifecycle helpers shared by all USB-based cores.
 *
 * Cores that enumerate over USB (Switch, XInput, SInput, Slippi) share the
 * same start/stop/task plumbing around the transport layer. This header
 * factors that boilerplate out into a small ::core_usb_state_t the core owns,
 * including an optional hook to stamp WAKE-supplied VID/PID into the core's
 * descriptors before enumeration.
 */

#ifndef CORE_USB_H
#define CORE_USB_H

#include <stdbool.h>
#include <stdint.h>
#include <dongle.h>
#include "cores/cores.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-core USB transport state.
 */
typedef struct
{
    core_params_s *params;  /**< Owning core's parameter block */
    bool transport_active;  /**< True once the USB transport has started */
} core_usb_state_t;

/**
 * @brief Optional hook to apply WAKE-supplied VID/PID to a core's descriptors.
 * @param wake WAKE packet carrying optional vid/pid overrides.
 * @param hid  Descriptor set to patch in place before enumeration.
 */
typedef void (*core_usb_apply_wake_t)(const dongle_wake_s *wake, core_hid_device_t *hid);

/**
 * @brief Service the USB transport for one tick if it is active.
 * @param state     Core USB state.
 * @param timestamp Current time in microseconds.
 */
void core_usb_task(core_usb_state_t *state, uint64_t timestamp);

/**
 * @brief Start the USB transport for a core, optionally applying WAKE overrides.
 * @param state      Core USB state to start.
 * @param wake       WAKE packet driving this session.
 * @param apply_wake Optional hook to patch descriptors from @p wake, or NULL.
 * @return true if the transport is active (already running or started); false on failure.
 */
bool core_usb_start(core_usb_state_t *state, const dongle_wake_s *wake, core_usb_apply_wake_t apply_wake);

/**
 * @brief Mark the USB transport inactive for a core.
 * @param state Core USB state to stop.
 */
void core_usb_stop(core_usb_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
