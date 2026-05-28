/*
 * Console transport abstraction interface (selection + event types).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file transport.h
 * @brief Public interface for the dongle->console transport abstraction layer.
 *
 * Defines the common transport selection entry points (transport_init /
 * transport_stop) plus the shared event and connection types used to drive a
 * concrete transport backend (USB device, Joybus N64, Joybus GameCube, ...).
 * A single backend is active at a time, chosen from the requested core_params.
 */

#ifndef TRANSPORT_TRANSPORT_H
#define TRANSPORT_TRANSPORT_H 

#include <stdbool.h>
#include <stdint.h>
#include <hoja_types.h>

#include "cores/cores.h"

/** Periodic transport service routine; called with the current timestamp (us). */
typedef void (*transport_task_t)(uint64_t timestamp);

/** Event kind delivered to a transport backend; selects the active tp_evt_s union member. */
typedef enum
{
    TP_EVT_PLAYERLED,       /**< Host assigned a player/LED slot. */
    TP_EVT_CONNECTIONCHANGE, /**< Console-side connection state changed. */
    TP_EVT_ERMRUMBLE,       /**< Rumble (ERM-style) intensity update. */
    TP_EVT_POWERCOMMAND,    /**< Host requested a power action (shutdown/reboot). */
} tp_evt_t;

/** Connection state reported by a transport backend toward the console. */
typedef enum 
{
    TP_CONNECTION_NONE,         /**< No connection established. */
    TP_CONNECTION_CONNECTING,   /**< Handshake/enumeration in progress. */
    TP_CONNECTION_CONNECTED,    /**< Link is up and usable. */
    TP_CONNECTION_DISCONNECTED, /**< Link dropped after being connected. */
} tp_connectionchange_t;

/** Power action requested by the host/console. */
typedef enum
{
    TP_POWERCOMMAND_SHUTDOWN, /**< Request a full power-off. */
    TP_POWERCOMMAND_REBOOT,   /**< Request a reboot. */
} tp_powercommand_t;

/** Payload for TP_EVT_PLAYERLED. */
typedef struct
{
    uint8_t player_number; /**< Assigned player index / LED pattern. */
} tp_evt_playerled_s;

/** Payload for TP_EVT_CONNECTIONCHANGE. */
typedef struct
{
    tp_connectionchange_t connection; /**< New connection state. */
} tp_evt_connectionchange_s;

/** Payload for TP_EVT_ERMRUMBLE. */
typedef struct
{
    uint8_t left;       /**< Left actuator intensity. */
    uint8_t right;      /**< Right actuator intensity. */
    uint8_t leftbrake;  /**< Left trigger/brake actuator intensity. */
    uint8_t rightbrake; /**< Right trigger/brake actuator intensity. */
} tp_evt_ermrumble_s;

/** Payload for TP_EVT_POWERCOMMAND. */
typedef struct 
{
    tp_powercommand_t power_command; /**< Requested power action. */
} tp_evt_powercommand_s;

/** Tagged transport event: @ref evt selects which union member is valid. */
typedef struct
{
    tp_evt_t evt; /**< Event discriminator. */
    union 
    {
        tp_evt_playerled_s          evt_playernumber;   /**< Valid when evt == TP_EVT_PLAYERLED. */
        tp_evt_connectionchange_s   evt_connectionchange; /**< Valid when evt == TP_EVT_CONNECTIONCHANGE. */
        tp_evt_ermrumble_s          evt_ermrumble;      /**< Valid when evt == TP_EVT_ERMRUMBLE. */
        tp_evt_powercommand_s       evt_powercommand;   /**< Valid when evt == TP_EVT_POWERCOMMAND. */
    };
} tp_evt_s;

/**
 * @brief Initialize and activate the transport backend named by params.
 *
 * Selects a backend from params->core_transport, initializes it, and on success
 * installs the backend's periodic task into params->core_transport_task and
 * records its stop callback for transport_stop().
 *
 * @param params Core parameters carrying the requested transport and receiving
 *               the installed transport task pointer.
 * @return true if a backend was selected and initialized; false otherwise.
 */
bool transport_init(core_params_s *params);

/**
 * @brief Stop and tear down the currently active transport backend, if any.
 */
void transport_stop();

#endif