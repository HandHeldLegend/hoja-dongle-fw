/*
 * WLAN platform adapter entry point (Pico W / CYW43 + lwIP).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file dongle_network.h
 * @brief Core-1 WLAN task entry for the RP2350 (Pico 2 W) platform.
 *
 * The portable dongle host protocol lives in the hoja_lib_dongle library; this
 * adapter supplies the platform-specific Wi-Fi AP / UDP plumbing through the
 * dongle_api_host_wlan_hook_* callbacks and runs the library wlan task on core 1.
 */

#ifndef DONGLE_NETWORK_H
#define DONGLE_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Core 1 entry point: bring up the radio and run the wlan task forever.
 *
 * Launch with multicore_launch_core1(). It initializes the dongle host wlan
 * side (which brings the access point up via the platform hook) and then loops
 * dongle_api_host_wlan_task().
 */
void dongle_network_core1_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* DONGLE_NETWORK_H */
