/*
 * Joybus N64 transport backend (weak default stubs).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_joybus64.c
 * @brief Weak fallback implementation of the N64 Joybus transport backend.
 *
 * Provides no-op weak definitions so the transport layer links and degrades
 * gracefully when no real N64 Joybus backend is built in. A concrete
 * implementation overrides these symbols at link time.
 */

#include "transport/transport_joybus64.h"

/** @brief Weak no-op stop; overridden by a real N64 backend when present. */
__attribute__((weak)) void transport_jb64_stop()
{
    
}

/** @brief Weak init stub; reports the N64 backend as unavailable (returns false). */
__attribute__((weak)) bool transport_jb64_init(core_params_s *params)
{
    return false;
}

/** @brief Weak no-op periodic task; overridden by a real N64 backend when present. */
__attribute__((weak)) void transport_jb64_task(uint64_t timestamp)
{
    (void) timestamp;
}