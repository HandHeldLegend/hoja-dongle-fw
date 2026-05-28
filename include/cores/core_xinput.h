/*
 * XInput (Xbox 360) gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_xinput.h
 * @brief Public API for the XInput gamepad core.
 *
 * Presents the dongle as an Xbox 360 controller over USB (default VID 0x045E,
 * PID 0x028E) using a fixed 20-byte XInput report. Input reports are pulled
 * from the wireless link and forwarded to the host.
 */

#ifndef CORE_XINPUT_H
#define CORE_XINPUT_H

#include <stdbool.h>
#include "cores/cores.h"
#include <dongle.h>

/**
 * @brief Initialize the XInput core into @p params and start its USB transport.
 * @param params Core parameter block to populate.
 * @param wake   WAKE packet for this session (optional VID/PID override). When
 *               NULL the core is configured but the transport is not started.
 * @return true on success; false if the transport failed to start.
 */
bool core_xinput_init(core_params_s *params, const dongle_wake_s *wake);

#endif
