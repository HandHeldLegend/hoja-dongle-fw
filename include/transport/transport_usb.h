/*
 * USB device transport backend interface.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport_usb.h
 * @brief Public interface for the USB device transport backend.
 *
 * Declares the init/stop/task entry points the transport layer uses to drive
 * the dongle->console link over USB. The concrete implementation is provided
 * elsewhere; the symbols here default to weak no-op stubs when no USB backend
 * is linked in.
 */

#ifndef TRANSPORT_USB_H
#define TRANSPORT_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "cores/cores.h"

/** @brief Stop and tear down the USB transport. */
void transport_usb_stop();

/**
 * @brief Initialize the USB transport backend.
 * @param params Core parameters describing the desired USB device.
 * @return true on success; false if the backend is unavailable or init failed.
 */
bool transport_usb_init(core_params_s *params);

/**
 * @brief Periodic USB transport service routine.
 * @param timestamp Current time in microseconds.
 */
void transport_usb_task(uint64_t timestamp);

#endif