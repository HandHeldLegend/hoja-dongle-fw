/*
 * NES/SNES controller HAL (PIO emulation of the controller's shift registers).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file nesbus_hal.c
 * @brief Emulates an NES or SNES controller on the console's latch/clock/data lines.
 *
 * The console pulses latch, then clocks the buttons out one bit per clock. One
 * PIO state machine watches latch and raises an IRQ; the ISR restarts the data
 * state machine, which shifts the next 32-bit word out on each clock. The ISR
 * also reads how many bits the last read clocked: an NES reads 8, an SNES 16,
 * so the task knows which layout to load next.
 */

#include <string.h>
#include <hoja_types.h>

#include "cores/core_snes.h"
#include "transport/transport.h"
#include "transport/transport_nesbus.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "generated/nesbus.pio.h"

#include "utilities/interval.h"
#include <dongle_host.h>

#if !defined(NESBUS_LATCH_PIN) || !defined(NESBUS_DATA_PIN) || !defined(NESBUS_CLOCK_PIN)
#error "The NESBus HAL needs NESBUS_LATCH_PIN, NESBUS_DATA_PIN and NESBUS_CLOCK_PIN"
#endif

/* No latch for this long: the console is off or unplugged */
#define NESBUS_DISCONNECT_US (4u * 1000u * 1000u)

/* SNES word as shifted out, MSB first: 12 buttons, 4 high ID bits, then low */
typedef struct
{
    union
    {
        struct
        {
            uint32_t padding : 16;
            uint32_t blank   : 4;
            uint32_t r       : 1;
            uint32_t l       : 1;
            uint32_t x       : 1;
            uint32_t a       : 1;
            uint32_t dright  : 1;
            uint32_t dleft   : 1;
            uint32_t ddown   : 1;
            uint32_t dup     : 1;
            uint32_t start   : 1;
            uint32_t select  : 1;
            uint32_t y       : 1;
            uint32_t b       : 1;
        };
        uint32_t value;
    };
} snes_hal_input_s;

/* NES word as shifted out, MSB first: 8 buttons, then low */
typedef struct
{
    union
    {
        struct
        {
            uint32_t padding : 24;
            uint32_t dright  : 1;
            uint32_t dleft   : 1;
            uint32_t ddown   : 1;
            uint32_t dup     : 1;
            uint32_t start   : 1;
            uint32_t select  : 1;
            uint32_t b       : 1;
            uint32_t a       : 1;
        };
        uint32_t value;
    };
} nes_hal_input_s;

static nesbus_pio_sm_s _nesbus_sm = {0};
static core_params_s *_nesbus_params = NULL;

/* Next word for the data state machine, every button released until a report arrives */
static volatile uint32_t _nesbus_write_data = 0xFFFFFFFF;

/* An NES clocks fewer bits per latch than an SNES */
static volatile bool _nesbus_snesmode = true;

/* Set by the latch ISR: the console read the controller */
static volatile bool _nesbus_polled = false;

static bool _nesbus_connected = false;

static void _nesbus_set_connected(bool connected)
{
    if (connected == _nesbus_connected)
        return;

    _nesbus_connected = connected;
    dongle_api_host_transport_set_player(connected ? 1 : 0);
    dongle_api_host_transport_set_transport(connected);
}

static void _nesbus_set_inputbits(uint32_t bitcount)
{
    _nesbus_snesmode = (bitcount < 20) && (bitcount > 0);
}

static void _nesbus_isr_handler(void)
{
    if (pio_interrupt_get(_nesbus_sm.pio, 0))
    {
        pio_sm_set_enabled(_nesbus_sm.pio, _nesbus_sm.sm_data, false);
        pio_sm_exec(_nesbus_sm.pio, _nesbus_sm.sm_data, _nesbus_sm.movy_instr_data);
        pio_sm_exec(_nesbus_sm.pio, _nesbus_sm.sm_data, _nesbus_sm.push_instr_data);
        pio_sm_exec(_nesbus_sm.pio, _nesbus_sm.sm_data, _nesbus_sm.sety_instr_data);
        pio_sm_exec(_nesbus_sm.pio, _nesbus_sm.sm_data, _nesbus_sm.jump_instr_data);
        pio_sm_set_enabled(_nesbus_sm.pio, _nesbus_sm.sm_data, true);
        pio_interrupt_clear(_nesbus_sm.pio, 0);
        _nesbus_set_inputbits(pio_sm_get_blocking(_nesbus_sm.pio, _nesbus_sm.sm_data));
        _nesbus_polled = true;
    }
}

/* Lay the gamepad's SNES report out the way the console being served reads it */
static uint32_t _nesbus_word_from_report(const core_snes_report_s *report)
{
    if (_nesbus_snesmode)
    {
        snes_hal_input_s snes = {.value = 0};
        snes.blank  = 0xF;
        snes.r      = report->r;
        snes.l      = report->l;
        snes.x      = report->x;
        snes.a      = report->a;
        snes.dright = report->dright;
        snes.dleft  = report->dleft;
        snes.ddown  = report->ddown;
        snes.dup    = report->dup;
        snes.start  = report->start;
        snes.select = report->select;
        snes.y      = report->y;
        snes.b      = report->b;
        return snes.value;
    }

    nes_hal_input_s nes = {.value = 0};
    nes.dright = report->dright;
    nes.dleft  = report->dleft;
    nes.ddown  = report->ddown;
    nes.dup    = report->dup;
    nes.start  = report->start;
    nes.select = report->select;
    nes.b      = report->b;
    nes.a      = report->a;
    return nes.value;
}

/***********************************************/
/********* Transport Defines *******************/

/** @brief Stop the NESBus transport: release the PIO and report the console gone. */
void transport_nesbus_stop()
{
    nesbus_pio_deinit(&_nesbus_sm, _nesbus_isr_handler);
    _nesbus_params = NULL;
    _nesbus_set_connected(false);
}

/**
 * @brief Start the NESBus transport for the given core parameters.
 *
 * @param params Core parameters (must use CORE_REPORTFORMAT_SNES).
 * @return true if the transport was started, false on format mismatch.
 */
bool transport_nesbus_init(core_params_s *params)
{
    if (params->core_report_format != CORE_REPORTFORMAT_SNES)
        return false;

    _nesbus_params = params;
    _nesbus_write_data = 0xFFFFFFFF;
    _nesbus_snesmode = true;
    _nesbus_polled = false;
    _nesbus_connected = false;

    nesbus_pio_init(&_nesbus_sm, _nesbus_isr_handler, NESBUS_LATCH_PIN, NESBUS_DATA_PIN, NESBUS_CLOCK_PIN);
    return true;
}

/**
 * @brief Per-tick service for the NESBus transport.
 *
 * Paces the wireless link off console reads, refreshes the next word at the
 * core poll rate, keeps the data state machine fed, and drops the connection
 * when the console stops latching.
 *
 * @param timestamp Current time in microseconds.
 */
void transport_nesbus_task(uint64_t timestamp)
{
    if (!_nesbus_params || !_nesbus_sm.running)
        return;

    static interval_s interval = {0};
    static interval_s interval_reset = {0};

    if (interval_run(timestamp, _nesbus_params->core_pollrate_us, &interval))
    {
        core_report_s report;
        if (core_get_generated_report(&report))
        {
            // Copied out: the report bytes are not word aligned
            core_snes_report_s snes;
            memcpy(&snes, report.data, sizeof(snes));
            _nesbus_write_data = _nesbus_word_from_report(&snes);
        }
    }

    if (pio_sm_is_tx_fifo_empty(_nesbus_sm.pio, _nesbus_sm.sm_data))
    {
        pio_sm_put(_nesbus_sm.pio, _nesbus_sm.sm_data, _nesbus_write_data);
    }

    const bool polled = _nesbus_polled;
    if (polled)
    {
        _nesbus_polled = false;
        dongle_api_host_transport_mark_sent();
        _nesbus_set_connected(true);
    }

    if (interval_resettable_run(timestamp, NESBUS_DISCONNECT_US, polled, &interval_reset))
    {
        _nesbus_set_connected(false);
    }
}
