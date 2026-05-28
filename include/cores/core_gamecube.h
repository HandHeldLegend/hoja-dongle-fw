/*
 * Nintendo GameCube controller gamepad core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_gamecube.h
 * @brief Public API and report layout for the GameCube controller core.
 *
 * Presents the dongle as a GameCube controller over the joybus transport.
 * Defines the controller state packet, including the several analog reporting
 * modes the console can request (mode0..mode4) which pack the stick, trigger,
 * and analog A/B values differently.
 */

#ifndef CORES_GAMECUBE_H
#define CORES_GAMECUBE_H

#include <stdint.h>
#include <stdbool.h>

#include "cores/cores.h"

/**
 * @brief GameCube controller state as reported over joybus.
 *
 * Two button bytes followed by an analog union whose active layout depends on
 * the analog mode requested by the console (the default unnamed layout is
 * mode 3; mode0..mode4 repack the same bytes for the other GameCube poll
 * modes).
 */
// Additional modes, ideas taken from
// https://github.com/PhobGCC/PhobGCC-SW/commit/dfeab45d68075e6df8cd3b303304b5169ffa8bea
// Thanks to https://github.com/mizuyoukanao
typedef struct
{
    union
    {
        struct
        {
            uint8_t a : 1; uint8_t b : 1; uint8_t x:1; uint8_t y : 1; uint8_t start : 1; uint8_t blank_1 : 3;
        };
        uint8_t buttons_1;  /**< A/B/X/Y/Start as a packed byte */
    };

    union
    {
        struct
        {
            uint8_t dpad_left : 1; uint8_t dpad_right : 1; uint8_t dpad_down : 1; uint8_t dpad_up : 1; uint8_t z : 1; uint8_t r : 1; uint8_t l : 1; uint8_t blank_2 : 1;
        };
        uint8_t buttons_2;  /**< D-pad + Z/R/L as a packed byte */
    };

    /** Analog payload; active member selected by the console's analog mode. */
    union
    {
        struct
        {
            uint8_t stick_left_x;
            uint8_t stick_left_y;
            uint8_t stick_right_x;
            uint8_t stick_right_y;
            uint8_t analog_trigger_l;
            uint8_t analog_trigger_r;
        }; /**< Default layout (mode 3): full 8-bit sticks and triggers */

        struct
        {
            uint8_t stick_left_x;
            uint8_t stick_left_y;
            uint8_t stick_right_x;
            uint8_t stick_right_y;
            uint8_t analog_trigger_r : 4;
            uint8_t analog_trigger_l : 4;
            uint8_t analog_b : 4;
            uint8_t analog_a : 4;
        } mode0; /**< 4-bit triggers and analog A/B */

        struct
        {
            uint8_t stick_left_x;
            uint8_t stick_left_y;
            uint8_t stick_right_y : 4;
            uint8_t stick_right_x : 4;
            uint8_t analog_trigger_l;
            uint8_t analog_trigger_r;
            uint8_t analog_b : 4;
            uint8_t analog_a : 4;
        } mode1; /**< 4-bit right stick, full triggers, 4-bit analog A/B */

        struct
        {
            uint8_t stick_left_x;
            uint8_t stick_left_y;
            uint8_t stick_right_y : 4;
            uint8_t stick_right_x : 4;
            uint8_t analog_trigger_r : 4;
            uint8_t analog_trigger_l : 4;
            uint8_t analog_a;
            uint8_t analog_b;
        } mode2; /**< 4-bit right stick and triggers, full analog A/B */

        struct
        {
            uint8_t stick_left_x;
            uint8_t stick_left_y;
            uint8_t stick_right_x;
            uint8_t stick_right_y;
            uint8_t analog_a;
            uint8_t analog_b;
        } mode4; /**< Full sticks and full analog A/B (no triggers) */
    };
} core_gamecube_report_s;
/** Size in bytes of a GameCube controller poll response. */
#define CORE_GAMECUBE_REPORT_SIZE sizeof(core_gamecube_report_s)

/**
 * @brief Initialize the GameCube core into @p params and start its joybus transport.
 * @param params Core parameter block to populate.
 * @return true if the joybus transport initialized successfully; false otherwise.
 */
bool core_gamecube_init(core_params_s *params);

#endif