/*
 * USB device transport backend (weak default stubs).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_usb.c
 * @brief Weak fallback implementation of the USB device transport backend.
 *
 * Provides no-op weak definitions so the transport layer links and degrades
 * gracefully when no real USB backend is built in. A concrete USB
 * implementation overrides these symbols at link time.
 */

#include "transport/transport_usb.h"

/** @brief Weak no-op stop; overridden by a real USB backend when present. */
__attribute__((weak)) void transport_usb_stop()
{
    
}

/** @brief Weak init stub; reports the USB backend as unavailable (returns false). */
__attribute__((weak)) bool transport_usb_init(core_params_s *params)
{
    return false;
}

/** @brief Weak no-op periodic task; overridden by a real USB backend when present. */
__attribute__((weak)) void transport_usb_task(uint64_t timestamp)
{
    (void) timestamp;
}