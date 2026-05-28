/*
 * Shared gamepad "core" dispatch layer
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file cores.h
 * @brief Common types and dispatch API for gamepad core personalities.
 *
 * A "core" is a single gamepad personality (Switch Pro, XInput, SInput,
 * Slippi/GameCube adapter, N64, GameCube) that fills out a ::core_params_s
 * with the callbacks and descriptors needed to drive its transport. The
 * active core is selected at runtime by core_init() based on the WAKE
 * packet's session mode, and the rest of the firmware interacts with
 * whichever core is active only through the generic helpers declared here.
 */

#ifndef CORES_CENTRAL_H
#define CORES_CENTRAL_H
#include <stdint.h>

#include <hoja_usb.h>
#include <hoja_types.h>
#include <dongle.h>

/**
 * @brief Report wire format produced by the active core.
 *
 * Selects how core_report_s payloads are interpreted and which USB transport
 * driver is used. CORE_REPORTFORMAT_UNDEFINED marks an uninitialized core.
 */
typedef enum 
{
    CORE_REPORTFORMAT_UNDEFINED = -1,
    CORE_REPORTFORMAT_SWPRO,
    CORE_REPORTFORMAT_XINPUT,
    CORE_REPORTFORMAT_SINPUT,
    CORE_REPORTFORMAT_SLIPPI,
    CORE_REPORTFORMAT_SNES,
    CORE_REPORTFORMAT_N64,
    CORE_REPORTFORMAT_GAMECUBE
} core_reportformat_t;

/**
 * @brief USB descriptor set advertised by a core when it enumerates.
 *
 * Cores that present as USB devices point ::core_params_s.hid_device at one of
 * these. WAKE-supplied VID/PID may override the defaults at init time.
 */
typedef struct
{
    const uint8_t *hid_report_descriptor;     /**< HID report descriptor, or NULL for non-HID (e.g. XInput) */
    uint16_t hid_report_descriptor_len;       /**< Length of hid_report_descriptor in bytes */
    const uint8_t *config_descriptor;         /**< USB configuration descriptor */
    uint16_t config_descriptor_len;           /**< Length of config_descriptor in bytes */
    uint16_t vid;                             /**< USB vendor ID */
    uint16_t pid;                             /**< USB product ID */
    char name[64];                            /**< Human-readable device name */
    const hoja_usb_device_descriptor_t *device_descriptor; /**< USB device descriptor */
} core_hid_device_t;

/** Maximum payload size (bytes) of a generated core report. */
#define CORE_REPORT_DATA_LEN 64
/**
 * @brief A single generated input report ready for the transport layer.
 */
typedef struct
{
    core_reportformat_t reportformat; /**< Format the data is encoded in */
    uint16_t size;                     /**< Valid byte count in data[] */
    uint8_t data[CORE_REPORT_DATA_LEN];
} core_report_s;

/** Generate the next outbound input report; returns false if unavailable. */
typedef bool (*core_generate_report_t)(core_report_s *out);
/** Feed a host->gamepad input report into the core. */
typedef void (*core_input_report_tunnel_t)(const uint8_t *data, uint16_t len);
/** Deliver a host->device OUT report (rumble, LEDs, feature commands) to the core. */
typedef void (*core_output_report_tunnel_t)(const uint8_t *data, uint16_t len);
/** Per-tick transport servicing callback. */
typedef void (*core_transport_task_t)(uint64_t timestamp);
/** Per-tick core servicing callback. */
typedef void (*core_task_t)(uint64_t timestamp);
/** Tear down core-owned resources on shutdown. */
typedef void (*core_deinit_t)(void);

/**
 * @brief Runtime descriptor for the active core personality.
 *
 * Each core_*_init() populates this with its callbacks, transport selection,
 * poll rate, and (for USB cores) its descriptor set. The dispatch helpers
 * below route generic firmware calls to whichever core owns these fields.
 */
typedef struct 
{
    core_task_t                   core_task;            /**< Per-tick core work */
    core_transport_task_t         core_transport_task;  /**< Per-tick transport work */
    gamepad_transport_t           core_transport;       /**< Transport backend (USB, joybus, etc.) */
    core_reportformat_t           core_report_format;   /**< Report wire format */
    uint16_t                      core_pollrate_us;     /**< Desired poll interval in microseconds */
    core_generate_report_t        core_report_generator;   /**< Produces outbound reports */
    core_output_report_tunnel_t   core_output_report_tunnel;  /**< Optional host OUT report sink */
    core_deinit_t                 core_deinit;          /**< Optional teardown hook */
    const core_hid_device_t*      hid_device;           /**< USB descriptor set, or NULL for non-USB cores */
} core_params_s;

/**
 * @brief Get a pointer to the active core's parameter block.
 * @return Pointer to the live ::core_params_s (never NULL).
 */
core_params_s* core_current_params();

/**
 * @brief Generate the next input report from the active core.
 * @param out Destination report buffer to fill.
 * @return true if a report was generated; false if the core has no generator.
 */
bool core_get_generated_report(core_report_s *out);

/**
 * @brief Forward an input report into the active core, if it accepts one.
 * @param data Pointer to the report bytes.
 * @param len  Length of the report in bytes (ignored when zero).
 */
void core_input_report_tunnel(const uint8_t *data, uint16_t len);

/**
 * @brief Service the active core for one tick.
 * @param timestamp Current time in microseconds.
 */
void core_task(uint64_t timestamp);

/**
 * @brief Tear down the active core and reset parameters to defaults.
 */
void core_deinit();

/**
 * @brief Default WAKE for power-on / link-timeout restore (N64 joybus).
 * @return Pointer to a static WAKE packet describing the boot session.
 */
const dongle_wake_s *core_boot_wake(void);

/**
 * @brief Select core + transport from wake->session.mode; USB cores apply wake->vid/pid.
 * @param wake WAKE packet whose session mode chooses the core to start.
 * @return true if the matching core initialized successfully; false otherwise.
 */
bool core_init(const dongle_wake_s *wake);

#endif
