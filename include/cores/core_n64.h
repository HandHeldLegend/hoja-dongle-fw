/*
 * Nintendo 64 controller gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_n64.h
 * @brief Public API and report layout for the Nintendo 64 controller core.
 *
 * Presents the dongle as an N64 controller over the joybus transport. Defines
 * the N64 controller state packet that is sent in response to console polls.
 */

#ifndef CORES_N64_H
#define CORES_N64_H

#include <stdint.h>
#include <stdbool.h>

#include "cores/cores.h"

/**
 * @brief N64 controller state as reported over joybus.
 *
 * Matches the byte/bit order the N64 console expects in a controller poll
 * response: two button bytes followed by signed analog stick axes.
 */
typedef struct
{
    union
    {
        struct
        {
            uint8_t dpad_right : 1;
            uint8_t dpad_left : 1;
            uint8_t dpad_down : 1;
            uint8_t dpad_up : 1;
            uint8_t button_start : 1;
            uint8_t button_z : 1;
            uint8_t button_b : 1;
            uint8_t button_a : 1;
        };
        uint8_t buttons_1;  /**< D-pad + Start/Z/B/A as a packed byte */
    };

    union
    {
        struct
        {
            uint8_t cpad_right : 1;
            uint8_t cpad_left : 1;
            uint8_t cpad_down : 1;
            uint8_t cpad_up : 1;
            uint8_t button_r : 1;
            uint8_t button_l : 1;
            uint8_t reserved : 1; /**< Unused, reported as 0 */
            uint8_t reset : 1;    /**< L+R+Start reset combo flag */
        };
        uint8_t buttons_2;  /**< C-buttons + L/R + reset as a packed byte */
    };

    int8_t stick_x; /**< Signed analog stick X axis */
    int8_t stick_y; /**< Signed analog stick Y axis */
} core_n64_report_s;
/** Size in bytes of an N64 controller poll response. */
#define CORE_N64_REPORT_SIZE sizeof(core_n64_report_s)

/**
 * @brief Initialize the N64 core into @p params and start its joybus transport.
 * @param params Core parameter block to populate.
 * @return true if the joybus transport initialized successfully; false otherwise.
 */
bool core_n64_init(core_params_s *params);

#endif