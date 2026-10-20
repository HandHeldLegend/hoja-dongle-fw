/*
 * Switch 2 GameCube controller (NSO) core
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file core_ns2gc.h
 * @brief The dongle as the Switch 2's GameCube controller (NSO), through NS2-LIB-HID.
 *
 * A gamepad in Slippi mode chosen at power-up sends the controller's input beside the adapter's
 * (DONGLE_WAKE_FLAG_NS2GC). On a Switch the dongle starts as the controller and finds out which
 * one from its first requests, as the gamepads do on a cable:
 *   - a computer asks for more than 8 bytes of device descriptor first (both Switches ask for 8)
 *   - a Switch 1 reads a string before configuring the device; a Switch 2 reads none, and makes
 *     its vendor request 0x03 right after configuring
 * A Switch 2 keeps the controller. Anything else gets the GameCube adapter (Slippi), at
 * Nintendo's 8 ms on a Switch 1.
 */

#ifndef CORES_NS2GC_H
#define CORES_NS2GC_H

#include <stdbool.h>
#include <stdint.h>

#include <dongle.h>

#include "cores/cores.h"
#include "hhl_tusb.h"

/** @brief This gamepad's session should start as the controller. */
bool core_ns2gc_wanted(const dongle_wake_s *wake);

/** @brief Initialize the core into @p params and start USB. */
bool core_ns2gc_init(core_params_s *params, const dongle_wake_s *wake);

/** @brief The controller is running (USB takes core_ns2gc_usb_config). */
bool core_ns2gc_running(void);

/** @brief Slippi on a console: Nintendo's 8 ms polling. */
bool core_ns2gc_console_rate(void);

/** @brief The USB personality for the driver library: descriptors, vendor requests, commands. */
void core_ns2gc_usb_config(hhl_tusb_config_s *tcfg);

/** @brief What the host asks while connecting. */
void core_ns2gc_usb_device_requested(uint16_t length);
void core_ns2gc_usb_string_requested(uint8_t index);
void core_ns2gc_usb_configured(void);

#endif
