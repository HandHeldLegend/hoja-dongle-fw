/*
 * The config app through the dongle: WebUSB commands for the dongle itself, and a tunnel to the
 * connected gamepad for everything else.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#ifndef DONGLE_WEBUSB_H
#define DONGLE_WEBUSB_H

#include <stdint.h>

/*
 * Dongle commands (the gamepad never answers these IDs, so the app can tell a dongle apart):
 *
 * 0xD0 info, reply (64 bytes):
 *   [0]  0xD0
 *   [1]  protocol (DONGLE_PROTOCOL_VERSION)
 *   [2]  board (dongle_board_t)
 *   [3]  1 while a gamepad is connected
 *   [4]  dongle firmware version, u32 little endian
 *   [8]  PIN, u16 little endian (0-9999)
 *   [10] host kind (dongle_host_kind_t)
 *   [11] host mode (dongle_mode_t)
 *   [12] gamepad mode (dongle_mode_t), 0xFF without a gamepad
 *   [13] gamepad flags (DONGLE_WAKE_FLAG_*)
 *   [14] gamepad firmware version, u32 little endian
 *   [18] gamepad USB VID, u16 little endian
 *   [20] gamepad USB PID, u16 little endian
 *   [22] gamepad name, 32 bytes, NUL terminated
 *
 * 0xD1 reboot into the USB bootloader, for a firmware update. No reply.
 *
 * 0xD2 PC mode: [1] dongle_mode_t (Switch, SInput or Slippi; not XInput, where Windows keeps the
 *   config interface from the app, so it couldn't change back). On a computer, the mode
 *   the dongle presents, until it is unplugged (then SInput). A gamepad that follows the dongle
 *   switches to it at once, and the dongle comes back on USB in that mode. Replies with the info
 *   above ([11] is the new mode); anything else (another mode, a console) changes nothing.
 */
#define DONGLE_WEBUSB_INFO       0xD0
#define DONGLE_WEBUSB_BOOTLOADER 0xD1
#define DONGLE_WEBUSB_PC_MODE    0xD2

/** WebUSB packet from the app (USB vendor callback). */
void dongle_webusb_rx(const uint8_t *data, uint16_t len);

/** Send what is waiting for the app; call from the USB transport task. */
void dongle_webusb_task(void);

#endif
