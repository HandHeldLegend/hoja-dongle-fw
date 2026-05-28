/*
 * SInput HID gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_sinput.h
 * @brief Public API for the SInput HID gamepad core.
 *
 * Presents the dongle as a standard HID SInput gamepad over USB. In addition
 * to streaming input reports, this core tunnels output reports (haptics,
 * player LEDs) to the gamepad and supports the SInput feature-report request
 * handshake.
 */

#ifndef CORES_SINPUT_H
#define CORES_SINPUT_H

#include "cores/cores.h"
#include <dongle.h>

/**
 * @brief Initialize the SInput core into @p params and start its USB transport.
 * @param params Core parameter block to populate.
 * @param wake   WAKE packet for this session (optional VID/PID override). When
 *               NULL the core is configured but the transport is not started.
 * @return true on success; false if the transport failed to start.
 */
bool core_sinput_init(core_params_s *params, const dongle_wake_s *wake);

#endif