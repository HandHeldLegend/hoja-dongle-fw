/*
 * WLAN PIN persistence for the dongle access point.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file dongle_pin.h
 * @brief Load, generate, and persist the 4-digit WLAN PIN (SSID/password).
 *
 * The PIN is stored in a dedicated flash sector as a small struct with a magic
 * value. On boot, dongle_pin_init() loads it; if the magic is invalid a new
 * PIN is generated and saved. At runtime, dongle_pin_regenerate_and_save()
 * queues a new PIN for flash, and dongle_pin_flush_save() blocks until the
 * write completes (call before rebooting).
 */

#ifndef DONGLE_PIN_H
#define DONGLE_PIN_H

#include <stdint.h>

/** Hold time for both-button PIN reset (microseconds). */
#ifndef HOJA_BTN_PIN_RESET_HOLD_US
#define HOJA_BTN_PIN_RESET_HOLD_US 10000000u
#endif

/**
 * @brief Load the PIN from flash (generating and persisting if needed).
 *
 * Call once on core 0 before the wlan task starts.
 */
void dongle_pin_init(void);

/** @brief Copy the active 4-digit PIN into @p pin (decimal digits 0-9). */
void dongle_pin_get(uint8_t pin[4]);

/**
 * @brief Generate a new PIN and queue a flash write.
 *
 * Does not block; pair with dongle_pin_flush_save() before rebooting.
 */
void dongle_pin_regenerate_and_save(void);

/** @brief Block until any queued PIN flash write has committed. */
void dongle_pin_flush_save(void);

#endif
