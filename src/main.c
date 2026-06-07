/*
 * Dongle firmware entry point (Pico 2 W).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file main.c
 * @brief Single application entry point built on the portable dongle host API.
 *
 * All dongle protocol/state-machine logic lives in the hoja_lib_dongle library,
 * split into a wlan task (radio, core 1) and a transport task (console, core 0).
 * This file is the only platform "glue" for the console side: it supplies the
 * generic util hooks (time / rand), the transport bring-up / teardown hooks that
 * map the gamepad's advertised personality onto a USB/Joybus core, and runs the
 * transport task + active core on core 0. The wlan side lives in dongle_network.c.
 */

#include <stdint.h>

#include <dongle.h>
#include <dongle_host.h>

#include "dongle_network.h"
#include "cores/cores.h"
#include "utilities/rgb.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"

#include "hardware/watchdog.h"

/* -------------------------------------------------------------------------- */
/* Generic util hooks (used by both library tasks)                            */
/* -------------------------------------------------------------------------- */

uint64_t dongle_api_hook_get_time_us_u64(void)
{
    return time_us_64();
}

uint16_t dongle_api_hook_get_rand_u16(void)
{
    return (uint16_t)get_rand_32();
}

/* -------------------------------------------------------------------------- */
/* Transport hooks (console personality lifecycle)                            */
/* -------------------------------------------------------------------------- */

/* A new gamepad session arrived: (re)select the USB/Joybus core matching the
 * advertised mode/vid/pid. Tear the previous core down first; re-enumerating in
 * place is unreliable, so we rebuild from scratch (matches the legacy path). */
void dongle_api_host_transport_hook_transport_bringup(const dongle_wake_s *wake)
{
    core_deinit();
    sleep_ms(100);
    core_init(wake);
}

/* The wireless link dropped (gamepad gone). Re-enumerating USB in place does
 * not reliably recover on the console host, so the most robust recovery is a
 * full reboot: it brings the transport back up clean and restores the default
 * boot session. */
void dongle_api_host_transport_hook_transport_teardown(void)
{
    watchdog_reboot(0, 0, 0);
}

/* -------------------------------------------------------------------------- */
/* Entry                                                                      */
/* -------------------------------------------------------------------------- */

int main(void)
{
    dongle_rgb_enter_bootloader_if_buttons_held();

    stdio_init_all();

    /* Present a default controller before any gamepad pairs (so a console sees a
     * device immediately). The first WAKE re-selects the gamepad's personality. */
    core_init(core_boot_wake());

    /* Core 1 owns the radio + the dongle host wlan task. */
    multicore_launch_core1(dongle_network_core1_entry);

    /* CYW43 may reconfigure GPIOs at init; reclaim button pins on core 0. */
    dongle_rgb_gpio_init();
    dongle_rgb_init();

    for (;;)
    {
        uint64_t now_us = time_us_64();

        dongle_rgb_task(now_us);

        /* Drains WAKE control packets (session adopt + transport bring-up) and
         * reacts to link-down (transport teardown). */
        dongle_api_host_transport_task();

        /* Service the active console core (input generation / transport TX). */
        core_task(now_us);
    }
}
