/*
 * Nintendo Switch Pro Controller gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_switch.h
 * @brief Public API for the Nintendo Switch Pro Controller core.
 *
 * Presents the dongle as a Switch Pro Controller over USB, pulling its USB
 * descriptors and 64-byte report format from the Nintendo Switch HID library.
 * Host OUT reports are tunneled back to the gamepad as reliable output.
 */

#ifndef CORE_SWITCH_H
#define CORE_SWITCH_H

#include <stdbool.h>
#include "cores/cores.h"
#include <dongle.h>

/**
 * @brief Initialize the Switch core into @p params and start its USB transport.
 * @param params Core parameter block to populate.
 * @param wake   WAKE packet for this session (optional VID/PID override). When
 *               NULL the core is configured but the transport is not started.
 * @return true on success; false if descriptors could not load or the transport
 *         failed to start.
 */
bool core_switch_init(core_params_s *params, const dongle_wake_s *wake);

#endif
