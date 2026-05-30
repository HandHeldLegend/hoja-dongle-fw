/*
 * GameCube Joybus controller HAL interface (PIO bit-banged).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file joybus_gc_hal.h
 * @brief Public interface for the GameCube Joybus controller HAL.
 *
 * Declares the lifecycle and per-tick entry points used to emulate a GameCube
 * controller on the host console's single-wire Joybus line. The HAL is driven
 * by a PIO state machine plus an interrupt handler; this header exposes only
 * the init/stop/task surface the rest of the firmware calls into.
 */

#ifndef HOJA_JOYBUS_GC_HAL_H
#define HOJA_JOYBUS_GC_HAL_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Tear down the GameCube Joybus HAL.
 *
 * Disables the PIO interrupt, removes the handler and PIO program, and resets
 * internal command/connection state so the transport can be cleanly stopped.
 */
void joybus_gc_hal_stop();

/**
 * @brief Initialize the GameCube Joybus HAL.
 *
 * Loads the Joybus PIO program, wires up the PIO interrupt handler, and starts
 * the state machine listening for console commands.
 *
 * @return true once the HAL is initialized and running.
 */
bool joybus_gc_hal_init();

/**
 * @brief Service the GameCube Joybus HAL once per main-loop tick.
 *
 * Pushes fresh input snapshots (mode-translated) to the PIO responder, handles
 * rumble updates, and detects communication loss to drive connection state.
 *
 * @param timestamp Current time in microseconds (monotonic).
 */
void joybus_gc_hal_task(uint64_t timestamp);

#endif