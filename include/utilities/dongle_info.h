/*
 * Which dongle this build is, for gamepads (beacon) and the config app (WebUSB).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#ifndef DONGLE_INFO_H
#define DONGLE_INFO_H

#include <dongle.h>

// The build's UTC timestamp, written by CMake at configure time (re-run CMake for a release)
#include "generated/dongle_version.h"

#if !defined(HOJA_USB_ONLY)
#define DONGLE_BOARD_ID DONGLE_BOARD_HOJA
#elif defined(HOJA_DONGLE_RP2350B_PLUS_W)
#define DONGLE_BOARD_ID DONGLE_BOARD_RP2350B_PLUS_W
#elif PICO_RP2350
#define DONGLE_BOARD_ID DONGLE_BOARD_PICO2_W
#else
#define DONGLE_BOARD_ID DONGLE_BOARD_PICO_W
#endif

#endif
