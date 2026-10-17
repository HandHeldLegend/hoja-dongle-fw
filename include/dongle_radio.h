/*
 * The dongle's radio: the Bluetooth link to the gamepad, on core 1.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#ifndef DONGLE_RADIO_H
#define DONGLE_RADIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Core 1 entry point: bring the radio up and run the link forever (dongle_link_bt.c). */
void dongle_radio_core1_entry(void);

/** True once the radio is up (USB waits for it: starting both at once upsets the radio's start). */
bool dongle_radio_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* DONGLE_RADIO_H */
