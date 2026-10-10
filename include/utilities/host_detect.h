/*
 * What the dongle is plugged into, for gamepads that follow it.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#ifndef HOST_DETECT_H
#define HOST_DETECT_H

#include <stdint.h>
#include <stdbool.h>

#include <dongle.h>

/**
 * @brief Start detecting the host after power-up.
 *
 * The HOJA board's console and USB share one port, so it listens as an N64 first (already
 * started at boot) and moves to USB if no console polls it. USB-only boards start with USB.
 * The result goes into the beacon through dongle_api_host_transport_set_detected().
 */
void host_detect_start(void);

/** @brief Drive detection from the core 0 loop. */
void host_detect_task(uint64_t now_us);

/** @brief What was found: DONGLE_HOST_UNKNOWN until detection is done (or nothing was found). */
void host_detect_get(dongle_host_kind_t *kind, dongle_mode_t *mode);

/** @brief USB host requests seen by the placeholder device (from the USB callbacks). */
void host_detect_on_usb_device(void);
void host_detect_on_usb_bos(void);

#endif
