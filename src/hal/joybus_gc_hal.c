/*
 * GameCube Joybus controller HAL (PIO bit-banged single-wire protocol).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file joybus_gc_hal.c
 * @brief Emulates a GameCube controller on the console's Joybus line via PIO + IRQ.
 *
 * A shared Joybus PIO program receives command bytes from the console and jumps
 * to its output routine to clock replies back out the single data wire. Console
 * commands arrive as a PIO interrupt; the time-critical handler decodes them
 * (probe, origin, poll, and the Swiss homebrew command) and pushes the
 * appropriate response. The GameCube poll command selects an analog "mode"
 * (0-4) that determines how stick/trigger/analog fields are packed, handled by
 * the mode translation step. The non-ISR task half feeds fresh, mode-translated
 * input snapshots, mirrors rumble to core0, and tears the link down on comms
 * loss. Reply timing is tuned with NOP spin delays to meet Joybus turnaround.
 */

#include <stdlib.h>
#include <hoja_types.h>

#include "cores/core_gamecube.h"
#include "transport/transport.h"
#include "transport/transport_joybusgc.h"

#include "hal/joybus_gc_hal.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "generated/joybus.pio.h"

#include "utilities/interval.h"
#include "utilities/crosscore_snapshot.h"
#include "core0transport.h"
#include "core1wlan.h"

/** GameCube Joybus command bytes the console sends to the controller. */
typedef enum
{
  GCUBE_CMD_PROBE = 0x00,     /**< Identify: report controller type/status. */
  GCUBE_CMD_POLL = 0x40,      /**< Request input report; carries mode + rumble bits. */
  GCUBE_CMD_ORIGIN = 0x41,    /**< Read neutral/origin calibration values. */
  GCUBE_CMD_ORIGINEXT = 0x42, /**< Recalibrate / extended origin request. */
  GCUBE_CMD_SWISS = 0x1D,     /**< Swiss homebrew loader probe variant. */
} gc_cmd_t;

/* Single-producer/single-consumer snapshot carrying the latest GameCube report
 * from the input task to the PIO poll responder. */
SNAPSHOT_TYPE(gcinput, core_gamecube_report_s);
snapshot_gcinput_t _gc_hal_snap;

core_params_s *_gc_core_params = NULL;

#define GC_PIO_IN_USE pio0
#define PIO_IRQ_USE_0 PIO0_IRQ_0
#define PIO_IRQ_USE_1 PIO0_IRQ_1

/* Joybus data pin: board override if provided, otherwise default to pin 1. */
#if !defined(JOYBUS_DRIVER_DATA_PIN)
#define JOYBUS_GC_DRIVER_DATA_PIN 1
#else
#define JOYBUS_GC_DRIVER_DATA_PIN JOYBUS_DRIVER_DATA_PIN
#endif

#define PIO_SM 0

#define CLAMP_0_255(value) ((value) < 0 ? 0 : ((value) > 255 ? 255 : (value)))
/* Joybus shifts MSB-first from the top of the 32-bit word, so an 8-bit reply
 * byte must be left-aligned into the high byte before being pushed to the PIO. */
#define ALIGNED_JOYBUS_8(val) ((val) << 24)

uint _gamecube_irq;        /**< PIO IRQ line number used by this HAL. */
uint _gamecube_offset;     /**< Instruction-memory offset of the loaded Joybus program. */
pio_sm_config _gamecube_c; /**< Cached state machine config from program init. */

volatile bool _gc_got_data = false;  /**< Set by the ISR whenever a command was handled (connection/watchdog). */
volatile bool _gc_sent_data = false; /**< Set after a poll reply so the task can pace the wireless link pump. */
bool _gc_running = false;
volatile bool _gc_rumble = false; /**< Latest rumble motor request decoded from a poll command. */
bool _gc_brake = false;           /**< Latest hard-stop (brake) request decoded from a poll command. */

volatile uint8_t _gamecube_in_buffer[8] = {0};

/**
 * @brief Reply to a PROBE command with the GameCube controller status word.
 */
void _gamecube_send_probe()
{
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x09));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, 0);
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x03));
}

/**
 * @brief Reply to an ORIGIN command with neutral/centered calibration values.
 */
void _gamecube_send_origin()
{
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, 0);
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x80));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x80));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x80));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x80));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x80));

  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x0)); // LT
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(0x0)); // RT
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, 0);
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, 0);
}

/**
 * @brief Reply to a POLL command with the latest button/stick/trigger snapshot.
 */
void _gamecube_send_poll()
{
  static core_gamecube_report_s out;
  snapshot_gcinput_read(&_gc_hal_snap, &out);
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.buttons_1));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.buttons_2));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.stick_left_x));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.stick_left_y));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.stick_right_x));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.stick_right_y));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.analog_trigger_l));
  pio_sm_put_blocking(GC_PIO_IN_USE, PIO_SM, ALIGNED_JOYBUS_8(out.analog_trigger_r));
}

/* Expected trailing-byte counts per command (counts down to 0). UNKNOWN means
 * the next byte is a fresh command opcode rather than a payload byte. */
#define BYTECOUNT_DEFAULT 2
#define BYTECOUNT_UNKNOWN -1
#define BYTECOUNT_SWISS 10
volatile int _byteCounter = BYTECOUNT_UNKNOWN; /**< Remaining payload bytes for the in-progress command. */
volatile uint8_t _workingCmd = 0x00;           /**< Command opcode currently being serviced. */
volatile uint8_t _workingMode = 0x03;          /**< Analog report mode selected by the last poll (default 3). */

/**
 * @brief Reset the command parser and re-init the Joybus PIO to receive-ready.
 */
void _gamecube_reset_state()
{
  _byteCounter = BYTECOUNT_UNKNOWN;
  joybus_program_init(GC_PIO_IN_USE, PIO_SM, _gamecube_offset, JOYBUS_GC_DRIVER_DATA_PIN, &_gamecube_c);
}

// Constants for default cycles and clock speeds
#define DEFAULT_CYCLES 80        // 80 was the tested working from old FW
#define DEFAULT_CLOCK_KHZ 125000 // 125 MHz
#define NEW_CLOCK_KHZ (HOJA_BSP_CLOCK_SPEED_HZ / 1000)
/* Per-command turnaround delays (in NOP spin cycles) so replies land inside the
 * console's expected Joybus response window for each command type. */
const uint32_t _gc_delay_cycles_origin = 50;
const uint32_t _gc_delay_cycles_probe = 50; // 100 was around 9us
const uint32_t _gc_delay_cycles_poll = 75;

/**
 * @brief Decode console command bytes from the PIO and queue the matching reply.
 *
 * Runs in interrupt/time-critical context. The first byte after an idle line is
 * the opcode; remaining payload bytes are counted down via _byteCounter.
 * Probe/origin reply immediately, poll captures mode + rumble/brake bits before
 * responding, and Swiss is consumed without a data reply. Reply paths spin a
 * tuned NOP delay before jumping the PIO to output to honor Joybus timing.
 */
void __time_critical_func(_gamecube_command_handler)()
{
  bool ret = false;
  uint8_t dat = 0;
  uint16_t c;

  // Single byte commands handle here
  if (_byteCounter == BYTECOUNT_UNKNOWN)
  {
    _workingCmd = pio_sm_get(GC_PIO_IN_USE, PIO_SM);

    if (_workingCmd == GCUBE_CMD_PROBE)
    {
      _gc_got_data = true;
      joybus_jump_output(GC_PIO_IN_USE, PIO_SM, _gamecube_offset);
      c = _gc_delay_cycles_probe;
      while (c--)
        asm("nop");
      _gamecube_send_probe();
      _byteCounter = BYTECOUNT_UNKNOWN;
      ret = true;
    }
    else if (_workingCmd == GCUBE_CMD_ORIGIN)
    {
      _gc_got_data = true;
      c = _gc_delay_cycles_origin;
      while (c--)
        asm("nop");
      joybus_jump_output(GC_PIO_IN_USE, PIO_SM, _gamecube_offset);
      _gamecube_send_origin();
      _byteCounter = BYTECOUNT_UNKNOWN;
      ret = true;
    }
    else if (_workingCmd == GCUBE_CMD_SWISS)
    {
      _byteCounter = BYTECOUNT_SWISS;
    }
    else
    {
      _byteCounter = BYTECOUNT_DEFAULT;
    }
  }
  else
  {

    dat = pio_sm_get(GC_PIO_IN_USE, PIO_SM);

    switch (_workingCmd)
    {
    default:
      break;

    case GCUBE_CMD_SWISS:
    {
      if (!_byteCounter)
      {
        _gc_got_data = true;
        _byteCounter = BYTECOUNT_UNKNOWN;
        _gamecube_reset_state();
        ret = true;
      }
    }
    break;

    case GCUBE_CMD_POLL:
    {
      if (_byteCounter == 1)
      {
        // Get our working mode
        _workingMode = dat;
      }
      else if (!_byteCounter)
      {
        _gc_got_data = true;
        // Final poll byte carries the rumble/brake command bits.
        _gc_rumble = (dat & 1) ? true : false;
        _gc_brake = (dat & 2) ? true : false;
        _byteCounter = BYTECOUNT_UNKNOWN;
        c = _gc_delay_cycles_poll;
        while (c--)
          asm("nop");
        joybus_jump_output(GC_PIO_IN_USE, PIO_SM, _gamecube_offset);
        _gamecube_send_poll();
        _gc_sent_data = true;
        ret = true;
      }
    }
    break;

    case GCUBE_CMD_ORIGINEXT:
    {
      if (!_byteCounter)
      {
        _gc_got_data = true;
        _byteCounter = BYTECOUNT_UNKNOWN;
        c = _gc_delay_cycles_poll;
        while (c--)
          asm("nop");
        joybus_jump_output(GC_PIO_IN_USE, PIO_SM, _gamecube_offset);
        _gamecube_send_origin();
        ret = true;
      }
    }
    break;
    }
  }

  // If no reply was sent, this byte was consumed as payload: count it down.
  if (!ret)
  {
    _byteCounter -= 1;
  }
}

/**
 * @brief PIO interrupt entry point; dispatches to the command handler.
 *
 * Masks the IRQ for the duration so the handler runs uninterrupted and clears
 * the PIO interrupt flag.
 */
static void __time_critical_func(_gamecube_isr_handler)(void)
{
  irq_set_enabled(_gamecube_irq, false);
  if (pio_interrupt_get(GC_PIO_IN_USE, 0))
  {
    pio_interrupt_clear(GC_PIO_IN_USE, 0);
    _gamecube_command_handler();
  }
  irq_set_enabled(_gamecube_irq, true);
}

/**
 * @brief Load the Joybus PIO program and arm its interrupt handler.
 *
 * @return true once the state machine is initialized and the IRQ is enabled.
 */
bool _joybus_gc_hal_init()
{
  _gamecube_offset = pio_add_program(GC_PIO_IN_USE, &joybus_program);

  _gamecube_irq = PIO_IRQ_USE_0;

  pio_set_irq0_source_enabled(GC_PIO_IN_USE, pis_interrupt0, true);

  irq_set_exclusive_handler(_gamecube_irq, _gamecube_isr_handler);

  irq_set_priority(PIO_IRQ_USE_0, 0);
  // irq_set_priority(PIO_IRQ_USE_1, 0);

  joybus_program_init(GC_PIO_IN_USE, PIO_SM, _gamecube_offset, JOYBUS_GC_DRIVER_DATA_PIN, &_gamecube_c);
  irq_set_enabled(_gamecube_irq, true);
  _gc_running = true;

  return true;
}

core_params_s *_gc_hal_params = NULL;

/**
 * @brief Repack a GameCube report into the layout the console's poll mode expects.
 *
 * The console selects an analog mode (0-4) via the poll command; each mode
 * trades resolution between the right stick, triggers, and analog A/B fields.
 * This copies @p in to @p out and then bit-shifts fields to the widths the
 * active @c _workingMode requires (mode 3 is the default, used as-is).
 *
 * @param mode Mode argument (informational; the active mode is read from _workingMode).
 * @param in   Source report with full-resolution fields.
 * @param out  Destination report packed for the active mode.
 */
void _jbgc_translate_data(uint8_t mode, core_gamecube_report_s *in, core_gamecube_report_s *out)
{
  out->blank_2 = 1;
  *out = *in;

  switch (_workingMode)
  {
  // Default is mode 3
  default:
    // Leave as-is
    break;

  case 0:
    out->mode0.stick_left_x = in->stick_left_x;
    out->mode0.stick_left_y = in->stick_left_y;
    out->mode0.stick_right_x = in->stick_right_x;
    out->mode0.stick_right_y = in->stick_right_y;
    out->mode0.analog_trigger_l = in->analog_trigger_l >> 4;
    out->mode0.analog_trigger_r = in->analog_trigger_r >> 4;
    out->mode0.analog_a = 0; // 4bits
    out->mode0.analog_b = 0; // 4bits
    break;

  case 1:
    out->mode1.stick_left_x = in->stick_left_x;
    out->mode1.stick_left_y = in->stick_left_y;
    out->mode1.stick_right_x = in->stick_right_x >> 4;
    out->mode1.stick_right_y = in->stick_right_y >> 4;
    out->mode1.analog_trigger_l = in->analog_trigger_l;
    out->mode1.analog_trigger_r = in->analog_trigger_r;
    out->mode1.analog_a = 0; // 4bits
    out->mode1.analog_b = 0; // 4bits
    break;

  case 2:
    out->mode2.stick_left_x = in->stick_left_x;
    out->mode2.stick_left_y = in->stick_left_y;
    out->mode2.stick_right_x = in->stick_right_x >> 4;
    out->mode2.stick_right_y = in->stick_right_y >> 4;
    out->mode2.analog_trigger_l = in->analog_trigger_l >> 4;
    out->mode2.analog_trigger_r = in->analog_trigger_r >> 4;
    out->mode2.analog_a = 0;
    out->mode2.analog_b = 0;
    break;

  case 4:
    out->mode4.stick_left_x = in->stick_left_x;
    out->mode4.stick_left_y = in->stick_left_y;
    out->mode4.stick_right_x = in->stick_right_x;
    out->mode4.stick_right_y = in->stick_right_y;
    out->mode4.analog_a = 0;
    out->mode4.analog_b = 0;
    break;
  }
}

/**
 * @brief Forward the decoded rumble state to core0's rumble output.
 */
void _jbgc_handle_rumble()
{
  // Handle rumble state if it changes
  static bool rumblestate = false;
  if (_gc_rumble != rumblestate)
  {
    rumblestate = _gc_rumble;
  }

  uint8_t rumble = rumblestate ? 255 : 0;

  core0_set_rumble(rumble, rumble, 0, 0);
}

/**
 * @brief Emit a connect/disconnect edge to core0 (player number + transport status).
 *
 * Only fires on a real state change.
 *
 * @param connected Current console-present state.
 */
void _jbgc_handle_connection(bool connected)
{
  // Handle connection state if it changes
  static uint8_t connectstate = 0;
  bool emit = false;
  if (!connectstate && connected)
  {
    connectstate = 1;
    emit = true;
  }
  else if (connectstate && !connected)
  {
    connectstate = 0;
    emit = true;
  }

  if (emit)
  {
    core0_set_player_number(connected ? 1 : 0);
    core0_set_transport_status(connected ? DONGLE_TRANSPORT_CONNECTED : DONGLE_TRANSPORT_IDLE);
  }
}

/**
 * @brief Recover from communication loss by re-arming the HAL in a neutral state.
 *
 * Re-inits the PIO, seeds a centered neutral input snapshot so the first poll
 * reply is neutral, signals disconnect, and resets wireless link pump timing.
 */
// Callback for the hardware alarm
static void _jbgc_reset()
{
  // Disable PIO IRQ to prevent races during re-init
  irq_set_enabled(_gamecube_irq, false);

  _gamecube_reset_state();

  core_gamecube_report_s neutral_report = {
      .stick_left_x = 128,
      .stick_left_y = 128,
      .stick_right_x = 128,
      .stick_right_y = 128,
      .buttons_1 = 0,
      .buttons_2 = 0,
      .analog_trigger_l = 0,
      .analog_trigger_r = 0,
      .blank_2 = 1 // Match current logic
  };

  // Ensure first poll response is neutral
  snapshot_gcinput_write(&_gc_hal_snap, &neutral_report);

  _jbgc_handle_connection(false);

  core1_link_pump_reset_timing();

  irq_set_enabled(_gamecube_irq, true);
}

/***********************************************/
/********* Transport Defines *******************/

/** @brief Stop the GameCube transport: tear down PIO/IRQ and clear all HAL state. */
void transport_jbgc_stop()
{
  // Disable the PIO IRQ first to prevent races
  irq_set_enabled(_gamecube_irq, false);

  // Remove the IRQ handler
  irq_remove_handler(_gamecube_irq, _gamecube_isr_handler);

  // Disable the PIO IRQ source
  pio_set_irq0_source_enabled(GC_PIO_IN_USE, pis_interrupt0, false);

  // Disable and clean up the state machine
  pio_sm_set_enabled(GC_PIO_IN_USE, PIO_SM, false);
  pio_sm_clear_fifos(GC_PIO_IN_USE, PIO_SM);

  // Remove the PIO program from instruction memory
  pio_remove_program(GC_PIO_IN_USE, &joybus_program, _gamecube_offset);

  // Reset internal state
  _byteCounter = BYTECOUNT_UNKNOWN;
  _workingCmd = 0x00;
  _workingMode = 0x03;
  _gc_got_data = false;
  _gc_sent_data = false;
  _gc_rumble = false;
  _gc_brake = false;
  _gc_running = false;
  core1_link_pump_reset_timing();

  // Notify disconnection
  _jbgc_handle_connection(false);

  _gc_hal_params = NULL;
}

/**
 * @brief Start the GameCube transport for the given core parameters.
 *
 * Rejects cores whose report format is not GameCube, then brings up the HAL.
 *
 * @param params Core parameters (must use CORE_REPORTFORMAT_GAMECUBE).
 * @return true if the transport was started, false on format mismatch.
 */
bool transport_jbgc_init(core_params_s *params)
{
  if (params->core_report_format != CORE_REPORTFORMAT_GAMECUBE)
    return false;
  _gc_hal_params = params;

  _joybus_gc_hal_init();
  return true;
}

/**
 * @brief Per-tick service for the GameCube transport.
 *
 * Paces the wireless link pump off poll replies, refreshes the mode-translated
 * input snapshot at the configured poll rate, services rumble, runs the 1 s
 * comms-loss watchdog, and reports connection on fresh traffic.
 *
 * @param timestamp Current time in microseconds.
 */
void transport_jbgc_task(uint64_t timestamp)
{
  if (!_gc_hal_params)
    return;

  static interval_s interval = {0};
  static interval_s interval_reset = {0};
  static interval_s interval_rumble = {0};

  if (_gc_sent_data)
  {
    uint64_t now_us = timestamp;
    core1_link_pump_schedule_from_poll(now_us);
    core1_link_pump_mark_sent(now_us);
    _gc_sent_data = false;
  }

  if (interval_run(timestamp, _gc_hal_params->core_pollrate_us, &interval))
  {
    // Get input report here
    core_report_s report;
    if (core_get_generated_report(&report))
    {
      core_gamecube_report_s translated = {0};
      _jbgc_translate_data(_workingMode, (core_gamecube_report_s *)report.data, &translated);
      snapshot_gcinput_write(&_gc_hal_snap, &translated);
    }
  }

  if (interval_run(timestamp, 32000, &interval_rumble))
  {
    // Rumblecore_gamecube_report_s
    _jbgc_handle_rumble();
  }

  // Check for communication loss (1 second timeout)
  if (interval_resettable_run(timestamp, 1000000, _gc_got_data, &interval_reset))
  {
    _jbgc_reset();
  }

  if (_gc_got_data)
  {
    _jbgc_handle_connection(true);
    _gc_got_data = false;
  }
}
