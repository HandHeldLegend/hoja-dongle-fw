/*
 * No-op board UI for official Raspberry Pi Pico W boards.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "utilities/rgb.h"

void dongle_rgb_gpio_init(void)
{
}

void dongle_rgb_init(void)
{
}

void dongle_rgb_set_mode(uint8_t dongle_mode)
{
    (void)dongle_mode;
}

void dongle_rgb_task(uint64_t now_us)
{
    (void)now_us;
}

void dongle_rgb_enter_bootloader_if_buttons_held(void)
{
}
