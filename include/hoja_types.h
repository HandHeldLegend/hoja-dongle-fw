/*
 * Shared HOJA value types (RGB, connection/mode/method enums, status, haptics).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file hoja_types.h
 * @brief Common value types shared across the HOJA firmware.
 *
 * Defines the RGB pixel layout (compile-time channel order), the gamepad
 * connection/mode/method/transport enumerations, the aggregate status struct, and
 * the processed-haptics packet types.
 */

#ifndef HOJA_TYPES_H
#define HOJA_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define RGB_ORDER_GRB 1
#define RGB_ORDER RGB 2

#define RGB_DRIVER_ORDER RGB_ORDER_GRB

/**
 * @brief Single RGB pixel.
 *
 * The byte order of the bitfield is selected at compile time to match the LED
 * driver's wire order, so @c color can be shifted out directly.
 */
// Handle RGB mode choosing compiler side
#if (RGB_DRIVER_ORDER == RGB_ORDER_GRB)
typedef struct
{
    union
    {
        struct
        {
            uint8_t padding : 8;
            uint8_t b : 8;
            uint8_t r : 8;
            uint8_t g : 8;
        };
        uint32_t color;
    };
} rgb_s;
#else 
typedef struct
{
    union
    {
        struct
        {
            uint8_t padding : 8;
            uint8_t b : 8;
            uint8_t g : 8;
            uint8_t r : 8;
        };
        uint32_t color;
    };
} rgb_s;
#endif

/** @brief Connection lifecycle / assigned-player state (negative values are transient states). */
typedef enum 
{
    CONN_STATUS_SHUTDOWN = -3,
    CONN_STATUS_INIT = -2,
    CONN_STATUS_DISCONNECTED  = -1,
    CONN_STATUS_CONNECTING  = 0,
    CONN_STATUS_PLAYER_1    = 1,
    CONN_STATUS_PLAYER_2    = 2,
    CONN_STATUS_PLAYER_3    = 3,
    CONN_STATUS_PLAYER_4    = 4,
    CONN_STATUS_PLAYER_5    = 5,
    CONN_STATUS_PLAYER_6    = 6,
    CONN_STATUS_PLAYER_7    = 7,
    CONN_STATUS_PLAYER_8    = 8,
} connection_status_t;

typedef enum 
{
    PLAYER_NUMBER_INIT = -1,
    
} player_number_t;

/** @brief Gamepad personality / emulated controller mode. */
typedef enum
{
    GAMEPAD_MODE_UNDEFINED = -2,
    GAMEPAD_MODE_LOAD     = -1, // Firmware load (bluetooth)
    GAMEPAD_MODE_SWPRO    = 0,
    GAMEPAD_MODE_XINPUT   = 1,
    GAMEPAD_MODE_GCUSB    = 2,
    GAMEPAD_MODE_GAMECUBE = 3,
    GAMEPAD_MODE_N64      = 4,
    GAMEPAD_MODE_SNES     = 5,
    GAMEPAD_MODE_SINPUT   = 6,
    GAMEPAD_MODE_MAX,
} gamepad_mode_t;

/** @brief Power/connection method that backs the active gamepad mode. */
typedef enum
{
    GAMEPAD_METHOD_AUTO  = -1,      // Automatically determine if we are plugged or wireless
    GAMEPAD_METHOD_WIRED = 0,       // Used for modes that should retain power even when unplugged
    GAMEPAD_METHOD_USB   = 1,       // Use for USB modes where we should power off when unplugged
    GAMEPAD_METHOD_BLUETOOTH = 2,   // Wireless Bluetooth modes
    GAMEPAD_METHOD_WLAN = 3,        // Wireless WLAN modes (dongle)
} gamepad_method_t;

/** @brief Physical/logical transport carrying gamepad data. */
typedef enum 
{
    GAMEPAD_TRANSPORT_UNDEFINED = -1,
    GAMEPAD_TRANSPORT_NESBUS,
    GAMEPAD_TRANSPORT_JOYBUS64,
    GAMEPAD_TRANSPORT_JOYBUSGC,
    GAMEPAD_TRANSPORT_USB,
    GAMEPAD_TRANSPORT_BLUETOOTH,
    GAMEPAD_TRANSPORT_WLAN,
} gamepad_transport_t;

/** @brief Aggregate runtime gamepad status (mode, method, colors, notifications). */
typedef struct 
{
    int8_t connection_status;
    gamepad_mode_t gamepad_mode;
    gamepad_method_t gamepad_method;
    bool   init_status;
    rgb_s  notification_color;
    rgb_s  gamepad_color;
    bool   ss_notif_pending; // Single-shot notification pending
    rgb_s  ss_notif_color; // Single-shot notification color
    uint8_t debug_data;
} hoja_status_s;

/** @brief One processed haptic band (hi/lo amplitude + frequency increment), packable as a u64. */
typedef union
{
    struct
    {
        uint16_t    hi_amplitude_fixed;
        uint16_t    lo_amplitude_fixed;
        uint16_t    hi_frequency_increment;
        uint16_t    lo_frequency_increment;
    };
    uint64_t value; 
} haptic_processed_s;

/** @brief A batch of processed haptic bands with a monotonic sequence counter. */
typedef struct
{
    haptic_processed_s pairs[3];
    uint8_t count;
    uint64_t counter; 
} haptic_packet_s;

#endif