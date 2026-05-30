/*
 * Slippi / Nintendo GameCube Adapter gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_slippi.h
 * @brief Public API and report layout for the Slippi / GameCube adapter core.
 *
 * Presents the dongle as an official Nintendo GameCube Controller Adapter
 * (VID 0x057E, PID 0x0337) over a custom USB class, the form expected by
 * Slippi/Dolphin. Defines the per-port controller report block that makes up
 * the adapter's input payload.
 */

#ifndef CORES_SLIPPI_H
#define CORES_SLIPPI_H

#include "cores/cores.h"

/**
 * @brief One controller port's input block within the GameCube adapter report.
 *
 * Mirrors the byte layout the Nintendo GameCube Adapter sends to the host for
 * a single port. Packed to one byte per field with no padding.
 */
#pragma pack(push, 1) // Ensure byte alignment
typedef struct
{
    union
    {
        struct
        {
            uint8_t button_a    : 1;
            uint8_t button_b    : 1;
            uint8_t button_x    : 1;
            uint8_t button_y    : 1;
            uint8_t dpad_left   : 1; //Left
            uint8_t dpad_right  : 1; //Right
            uint8_t dpad_down   : 1; //Down
            uint8_t dpad_up     : 1; //Up
        };
        uint8_t buttons_1;  /**< Face buttons + D-pad as a packed byte */
    };

    union
    {
        struct
        {
            uint8_t button_start: 1;
            uint8_t button_z    : 1;
            uint8_t button_r    : 1;
            uint8_t button_l    : 1;
            uint8_t blank1      : 4; /**< Unused padding bits */
        }; 
        uint8_t buttons_2;  /**< Start/Z/R/L as a packed byte */
    };

  uint8_t stick_x;   /**< Main stick X axis */
  uint8_t stick_y;   /**< Main stick Y axis */
  uint8_t cstick_x;  /**< C-stick X axis */
  uint8_t cstick_y;  /**< C-stick Y axis */
  uint8_t trigger_l; /**< Analog left trigger */
  uint8_t trigger_r; /**< Analog right trigger */

} core_slippi_report_s;
#pragma pack(pop)

/**
 * @brief Initialize the Slippi core into @p params and start its USB transport.
 * @param params Core parameter block to populate.
 * @param wake   WAKE packet for this session. When NULL the core is configured
 *               but the transport is not started.
 * @return true on success; false if the transport failed to start.
 */
bool core_slippi_init(core_params_s *params, const dongle_wake_s *wake);

#endif