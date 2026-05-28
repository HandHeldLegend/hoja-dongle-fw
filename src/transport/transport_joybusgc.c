/*
 * Joybus GameCube transport backend (weak default stubs).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_joybusgc.c
 * @brief Weak fallback implementation of the GameCube Joybus transport backend.
 *
 * Provides no-op weak definitions so the transport layer links and degrades
 * gracefully when no real GameCube Joybus backend is built in. A concrete
 * implementation overrides these symbols at link time.
 */

#include "transport/transport_joybusgc.h"

/** @brief Weak no-op stop; overridden by a real GameCube backend when present. */
__attribute__((weak)) void transport_jbgc_stop()
{
    
}

/** @brief Weak init stub; reports the GameCube backend as unavailable (returns false). */
__attribute__((weak)) bool transport_jbgc_init(core_params_s *params)
{
    return false;
}

/** @brief Weak no-op periodic task; overridden by a real GameCube backend when present. */
__attribute__((weak)) void transport_jbgc_task(uint64_t timestamp)
{
    (void) timestamp;
}