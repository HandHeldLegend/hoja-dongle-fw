/*
 * Public interface for the dongle status LEDs and front-panel buttons.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file rgb.h
 * @brief Build-time configuration and API for the mode/connection RGB LEDs.
 *
 * Declares the entry points used by the firmware to drive the two status LEDs
 * and handle the buttons that toggle them, plus the compile-time knobs
 * (brightness, pin assignments, button polarity, LED ordering) that adapt the
 * driver to a given board build.
 */

#ifndef RGB_H
#define RGB_H

#include <stdint.h>

/**
 * WS2812 brightness scale (0–255). Applied to all channel values before TX.
 * Override at build time: -DHOJA_RGB_BRIGHTNESS=32
 */
#ifndef HOJA_RGB_BRIGHTNESS
#define HOJA_RGB_BRIGHTNESS 32u
#endif

/** Hold time before a button toggles its LED (microseconds). */
#ifndef HOJA_BTN_HOLD_US
#define HOJA_BTN_HOLD_US 500000u
#endif

/** GPIO for BTN1 (mode LED toggle). */
#ifndef HOJA_BTN1_PIN
#define HOJA_BTN1_PIN 20
#endif

/** GPIO for BTN2 (connection LED toggle). */
#ifndef HOJA_BTN2_PIN
#define HOJA_BTN2_PIN 29
#endif

/** 1 = pressed reads low (button to GND). 0 = pressed reads high. */
#ifndef HOJA_BTN_ACTIVE_LOW
#define HOJA_BTN_ACTIVE_LOW 1
#endif

/** Set to 1 if the LED closest to the MCU is connection, not mode. */
#ifndef HOJA_RGB_LED_SWAP
#define HOJA_RGB_LED_SWAP 0
#endif

/** BTN1 (GPIO20): mode LED. BTN2 (GPIO29): WLAN / connection LED. */
#define DONGLE_RGB_LED_MODE 0u
#define DONGLE_RGB_LED_CONN 1u

/** @brief Initialize the button input GPIOs (called by dongle_rgb_init too). */
void dongle_rgb_gpio_init(void);

/** @brief Initialize the LED HAL and button state and show the initial frame. */
void dongle_rgb_init(void);

/**
 * @brief Set the controller mode shown on the mode LED.
 * @param dongle_mode dongle_mode_t value selecting the LED color.
 */
void dongle_rgb_set_mode(uint8_t dongle_mode);

/**
 * @brief Periodic LED/button service; call frequently from the main loop.
 * @param now_us Current time in microseconds (e.g. from time_us_64()).
 */
void dongle_rgb_task(uint64_t now_us);

/** Call once at boot; if BTN1+BTN2 are held, enters USB UF2 bootloader (noreturn). */
void dongle_rgb_enter_bootloader_if_buttons_held(void);

#endif
