/*
 * NES/SNES controller gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_snes.h
 * @brief Public API and report layout for the NES/SNES controller core.
 *
 * Presents the dongle as an SNES controller over the NESBus transport. An NES
 * reads fewer bits per latch, so the same core answers both consoles.
 */

#ifndef CORES_SNES_H
#define CORES_SNES_H

#include <stdint.h>
#include <stdbool.h>

#include "cores/cores.h"
#include <dongle.h>

/**
 * @brief SNES button state as the gamepad sends it over WLAN.
 *
 * Active low like the console expects (0 = pressed), the same layout as the
 * gamepad's SNES core.
 */
typedef struct
{
    union
    {
        struct
        {
            uint32_t padding : 20;
            uint32_t r      : 1;
            uint32_t l      : 1;
            uint32_t x      : 1;
            uint32_t a      : 1;
            uint32_t dright : 1;
            uint32_t dleft  : 1;
            uint32_t ddown  : 1;
            uint32_t dup    : 1;
            uint32_t start  : 1;
            uint32_t select : 1;
            uint32_t y      : 1;
            uint32_t b      : 1;
        };
        uint32_t value;
    };
} core_snes_report_s;
/** Size in bytes of an SNES button report. */
#define CORE_SNES_REPORT_SIZE sizeof(core_snes_report_s)

/**
 * @brief Initialize the SNES core into @p params and start its NESBus transport.
 * @param params Core parameter block to populate.
 * @param wake   WAKE packet supplying WLAN identity strings (optional).
 * @return true if the NESBus transport initialized successfully; false otherwise.
 */
bool core_snes_init(core_params_s *params, const dongle_wake_s *wake);

#endif
