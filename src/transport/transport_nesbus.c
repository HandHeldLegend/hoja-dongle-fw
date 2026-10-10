/*
 * NESBus (NES/SNES) transport backend (weak default stubs).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_nesbus.c
 * @brief Weak fallback implementation of the NESBus transport backend.
 *
 * Provides no-op weak definitions so the transport layer links and degrades
 * gracefully when no real NESBus backend is built in. A concrete
 * implementation overrides these symbols at link time.
 */

#include "transport/transport_nesbus.h"

/** @brief Weak no-op stop; overridden by a real NESBus backend when present. */
__attribute__((weak)) void transport_nesbus_stop()
{

}

/** @brief Weak init stub; reports the NESBus backend as unavailable (returns false). */
__attribute__((weak)) bool transport_nesbus_init(core_params_s *params)
{
    (void) params;
    return false;
}

/** @brief Weak no-op periodic task; overridden by a real NESBus backend when present. */
__attribute__((weak)) void transport_nesbus_task(uint64_t timestamp)
{
    (void) timestamp;
}
