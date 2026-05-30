/*
 * Joybus GameCube transport backend interface.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_joybusgc.h
 * @brief Public interface for the Joybus (Nintendo GameCube) transport backend.
 *
 * Declares the init/stop/task entry points the transport layer uses to drive
 * the dongle->console link over the GameCube Joybus protocol. The concrete
 * implementation is provided elsewhere; the symbols here default to weak no-op
 * stubs when no GameCube backend is linked in.
 */

#ifndef TRANSPORT_JBGC_H
#define TRANSPORT_JBGC_H

#include <stdbool.h>
#include <stdint.h>

#include "cores/cores.h"

/** @brief Stop and tear down the GameCube Joybus transport. */
void transport_jbgc_stop();

/**
 * @brief Initialize the GameCube Joybus transport backend.
 * @param params Core parameters for the GameCube transport.
 * @return true on success; false if the backend is unavailable or init failed.
 */
bool transport_jbgc_init(core_params_s *params);

/**
 * @brief Periodic GameCube Joybus transport service routine.
 * @param timestamp Current time in microseconds.
 */
void transport_jbgc_task(uint64_t timestamp);

#endif