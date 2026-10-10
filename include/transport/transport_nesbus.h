/*
 * NESBus (NES/SNES) transport backend interface.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_nesbus.h
 * @brief Public interface for the NESBus (NES/SNES controller port) transport backend.
 *
 * Declares the init/stop/task entry points the transport layer uses to drive
 * the dongle->console link as an NES or SNES controller. The concrete
 * implementation is provided elsewhere; the symbols here default to weak no-op
 * stubs when no NESBus backend is linked in.
 */

#ifndef TRANSPORT_NESBUS_H
#define TRANSPORT_NESBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "cores/cores.h"

/** @brief Stop and tear down the NESBus transport. */
void transport_nesbus_stop();

/**
 * @brief Initialize the NESBus transport backend.
 * @param params Core parameters for the SNES transport.
 * @return true on success; false if the backend is unavailable or init failed.
 */
bool transport_nesbus_init(core_params_s *params);

/**
 * @brief Periodic NESBus transport service routine.
 * @param timestamp Current time in microseconds.
 */
void transport_nesbus_task(uint64_t timestamp);

#endif
