/*
 * Joybus N64 transport backend interface.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_joybus64.h
 * @brief Public interface for the Joybus (Nintendo 64) transport backend.
 *
 * Declares the init/stop/task entry points the transport layer uses to drive
 * the dongle->console link over the N64 Joybus protocol. The concrete
 * implementation is provided elsewhere; the symbols here default to weak no-op
 * stubs when no N64 backend is linked in.
 */

#ifndef TRANSPORT_JB64_H
#define TRANSPORT_JB64_H

#include <stdbool.h>
#include <stdint.h>

#include "cores/cores.h"

/** @brief Stop and tear down the N64 Joybus transport. */
void transport_jb64_stop();

/**
 * @brief Initialize the N64 Joybus transport backend.
 * @param params Core parameters for the N64 transport.
 * @return true on success; false if the backend is unavailable or init failed.
 */
bool transport_jb64_init(core_params_s *params);

/**
 * @brief Periodic N64 Joybus transport service routine.
 * @param timestamp Current time in microseconds.
 */
void transport_jb64_task(uint64_t timestamp);

#endif