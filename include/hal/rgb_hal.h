/*
 * WS2812 addressable RGB LED HAL interface (PIO + DMA driven).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file rgb_hal.h
 * @brief Public interface for the WS2812 RGB LED HAL.
 *
 * Declares the entry points used to drive a chain of WS2812 ("NeoPixel")
 * addressable LEDs. Pixel data is shifted out by a PIO state machine fed via a
 * DMA channel, so updates are non-blocking from the caller's perspective.
 */

#ifndef HOJA_RGB_HAL_H
#define HOJA_RGB_HAL_H

#include <stdint.h>
#include <hoja_types.h>

/** Number of LEDs the driver allocates for; should be a power of two and may exceed the physical strip length. */
#ifndef HOJA_RGB_COUNT
#define HOJA_RGB_COUNT 32
#endif

/**
 * @brief Initialize the RGB HAL.
 *
 * Claims a DMA channel and PIO state machine, loads the WS2812 program, and
 * blanks the strip with an initial update.
 */
void rgb_hal_init(void);

/**
 * @brief Tear down the RGB HAL.
 *
 * Currently a no-op placeholder; reserved for releasing PIO/DMA resources.
 */
void rgb_hal_deinit(void);

/**
 * @brief Push pixel data to the LED strip.
 *
 * Copies @p data into the internal buffer (when non-NULL) and kicks off a DMA
 * transfer to the WS2812 PIO state machine. Pixels are packed GRB; the count
 * driven is HOJA_RGB_COUNT.
 *
 * @param data Pointer to HOJA_RGB_COUNT pixels, or NULL to re-send the current buffer.
 */
void rgb_hal_update(const rgb_s *data);

#endif