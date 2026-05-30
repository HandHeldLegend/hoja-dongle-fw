/*
 * Nintendo 64 Joybus controller HAL (PIO bit-banged single-wire protocol).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file joybus_n64_hal.c
 * @brief Emulates an N64 controller on the console's Joybus line via PIO + IRQ.
 *
 * A shared Joybus PIO program receives command bytes from the console and, when
 * the firmware decides to reply, jumps to its output routine to clock a
 * response back out the single data wire. Console commands arrive as a PIO
 * interrupt; the time-critical ISR/handler decodes them (probe, poll, mem-pak
 * read/write for rumble) and pushes the reply bytes. The non-ISR task half
 * feeds fresh input snapshots, mirrors rumble to core0, and tears the link down
 * on communication loss. Reply timing is tuned with NOP spin delays so the
 * controller answers within the console's tight Joybus turnaround window.
 */

#include <hoja_types.h>

#include "cores/core_n64.h"
#include "transport/transport.h"
#include "transport/transport_joybus64.h"

#include "hal/joybus_n64_hal.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "generated/joybus.pio.h"

#include "utilities/n64_crc.h"
#include "utilities/interval.h"
#include "utilities/crosscore_snapshot.h"
#include "core0transport.h"
#include "core1wlan.h"

/** N64 Joybus command bytes the console sends to the controller. */
typedef enum
{
  N64_CMD_PROBE = 0x00,    /**< Identify: report controller type and pak status. */
  N64_CMD_POLL = 0x01,     /**< Request the current button/stick report. */
  N64_CMD_READMEM = 0x02,  /**< Read 32 bytes from the mem/rumble pak address space. */
  N64_CMD_WRITEMEM = 0x03, /**< Write 32 bytes to the mem/rumble pak address space. */
  N64_CMD_RESET = 0xFF     /**< Reset: treated like probe here. */
} n64_cmd_t;

/* Single-producer/single-consumer snapshot carrying the latest N64 report from
 * the input task to the PIO poll responder. */
SNAPSHOT_TYPE(n64input, core_n64_report_s);
snapshot_n64input_t _n64_hal_snap;

core_params_s *_n64_hal_params = NULL;

/* Joybus data pin: board override if provided, otherwise default to pin 1. */
#if !defined(JOYBUS_DRIVER_DATA_PIN)
#define JOYBUS_N64_DRIVER_DATA_PIN 1
#else
#define JOYBUS_N64_DRIVER_DATA_PIN JOYBUS_DRIVER_DATA_PIN
#endif

#define PIO_IN_USE_N64 pio0
#define PIO_IRQ_USE_0 PIO0_IRQ_0
#define PIO_IRQ_USE_1 PIO0_IRQ_1

#define PIO_SM 0

#define CLAMP_0_255(value) ((value) < 0 ? 0 : ((value) > 255 ? 255 : (value)))
/* Joybus shifts MSB-first from the top of the 32-bit word, so an 8-bit reply
 * byte must be left-aligned into the high byte before being pushed to the PIO. */
#define ALIGNED_JOYBUS_8(val) ((val) << 24)
#define N64_RANGE 90
#define N64_RANGE_MULTIPLIER (N64_RANGE) / 4096

uint _n64_irq;            /**< PIO IRQ line number used by this HAL. */
uint _n64_offset;         /**< Instruction-memory offset of the loaded Joybus program. */
pio_sm_config _n64_c;     /**< Cached state machine config from program init. */
bool _n64_rumble = false; /**< Latest rumble motor request decoded from a pak write. */

volatile static uint8_t _workingCmd = 0x00; /**< Multi-byte command currently being assembled in the ISR. */
volatile static uint8_t _byteCount = 0;     /**< Bytes received so far for the in-progress command. */
volatile uint8_t _crc_reply = 0;            /**< Running CRC accumulated over a pak-write payload. */

volatile bool _n64_got_data = false;  /**< Set by the ISR whenever a command was handled (connection/watchdog). */
volatile bool _n64_sent_data = false; /**< Set after a poll reply so the task can pace the wireless link pump. */
bool _n64_running = false;

/**
 * @brief Fold a buffer into the N64 mem-pak CRC, with the final pak-presence twist.
 *
 * @param data         Bytes to checksum.
 * @param len          Number of bytes in @p data.
 * @param init_value   Starting CRC value.
 * @param pak_inserted When true, the result is inverted (0xFF) to signal a pak is present.
 * @return The computed 8-bit CRC.
 */
uint8_t _n64_calculate_crc(const uint8_t *data, size_t len, uint8_t init_value, bool pak_inserted)
{
  uint8_t crc = init_value;

  for (size_t i = 0; i < len; ++i)
  {
    crc = n64_crc[(crc ^ data[i])];
  }

  return crc ^ ((pak_inserted) ? 0xFF : 0x00);
}

/**
 * @brief Advance the mem-pak CRC by a single byte.
 *
 * @param crc   Current running CRC.
 * @param input Next payload byte.
 * @return The updated CRC.
 */
uint8_t _n64_iterate_crc(uint8_t crc, uint8_t input)
{
  uint8_t out = n64_crc[(crc ^ input)];
  return out;
}

/**
 * @brief Reply to a read of the rumble-pak identify region (0x8000).
 *
 * Emits 32 bytes of 0x80 followed by the precomputed CRC, the canonical
 * "rumble pak present" identify block.
 */
void _n64_send_rumble_identify()
{
  for (uint i = 0; i < 32; i++)
  {
    pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(0x80));
  }
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(0xB8)); // Precomputed CRC (0x47 or also try 0xB8)
}

/**
 * @brief Reply to a mem-pak read of a non-identify region with all zeroes.
 */
void _n64_send_null_identify()
{
  for (uint i = 0; i < 33; i++)
  {
    pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, 0);
  }
}

/**
 * @brief Reply to a PROBE/RESET command with the N64 controller status word.
 *
 * Reports the standard controller type and flags a pak as installed.
 */
void _n64_send_probe()
{
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(0x05));
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, 0);
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(0x01)); // Indicate PAK is installed
}

/**
 * @brief Acknowledge a completed pak write by returning the accumulated CRC.
 */
void _n64_send_pak_write()
{
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(_crc_reply));
}

/**
 * @brief Reply to a POLL command with the latest button/stick snapshot.
 */
void _n64_send_poll()
{
  static core_n64_report_s out;
  snapshot_n64input_read(&_n64_hal_snap, &out);
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(out.buttons_1));
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(out.buttons_2));
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(out.stick_x));
  pio_sm_put_blocking(PIO_IN_USE_N64, PIO_SM, ALIGNED_JOYBUS_8(out.stick_y));
}

#define PAK_MSG_BYTES 33 /**< Pak write payload length: 1 trailing index byte after 32 data bytes. */

// Constants for default cycles and clock speeds
// 1 cycle is about 0.05us delay time

#define DEFAULT_CYCLES 100
#define DEFAULT_CLOCK_KHZ 125000 // 125 MHz
#define NEW_CLOCK_KHZ (HOJA_BSP_CLOCK_SPEED_HZ / 1000)
/* Per-command turnaround delays (in NOP spin cycles) so replies land inside the
 * console's expected Joybus response window for each command type. */
const uint32_t _delay_cycles_memread = 120;
const uint32_t _delay_cycles_memwrite = 100;
const uint32_t _delay_cycles_probe = 100;
const uint32_t _delay_cycles_poll = 120;
uint8_t _n64_hal_in_buffer[64] = {0}; /**< Scratch buffer for incoming multi-byte command bytes. */

/**
 * @brief Decode one command byte from the PIO and, when complete, queue a reply.
 *
 * Runs in interrupt/time-critical context. Multi-byte commands (mem-pak
 * read/write) are reassembled across successive invocations using _workingCmd
 * and _byteCount; single-byte commands (probe/reset/poll) reply immediately.
 * Each reply path spins a tuned NOP delay before jumping the PIO to its output
 * routine so the response respects Joybus turnaround timing.
 */
void __time_critical_func(_n64_command_handler)()
{
  uint32_t c = DEFAULT_CYCLES;

  // Resume working on commands that are longer than 1 byte
  if (_workingCmd == N64_CMD_WRITEMEM)
  {
    uint16_t writeaddr = 0;
    _n64_hal_in_buffer[_byteCount] = pio_sm_get(PIO_IN_USE_N64, PIO_SM);

    if (_byteCount > 1) // Only 32 bytes after the address
      _crc_reply = _n64_iterate_crc(_crc_reply, _n64_hal_in_buffer[_byteCount]);

    if (_byteCount >= PAK_MSG_BYTES)
    {
      // First two bytes are the address; low 5 bits are the address CRC and are masked off.
      writeaddr = (_n64_hal_in_buffer[0] << 8) | (_n64_hal_in_buffer[1] & 0xE0);
      // 0xC000 is the rumble-pak motor control register: nonzero data spins the motor.
      if (writeaddr == 0xC000)
      {
        _n64_rumble = (_n64_hal_in_buffer[2] > 0) ? true : false;
      }
      _workingCmd = 0;
      _byteCount = 0;

      // WRITE
      // 200 delay cycles = ~12us
      // 100 delay cycles = ~7.5us
      c = _delay_cycles_memwrite;
      while (c--)
        asm("nop");

      joybus_jump_output(PIO_IN_USE_N64, PIO_SM, _n64_offset);
      _n64_send_pak_write();
    }
    else
      _byteCount++;
  }
  else if (_workingCmd == N64_CMD_READMEM)
  {

    _n64_hal_in_buffer[_byteCount] = pio_sm_get(PIO_IN_USE_N64, PIO_SM);

    // A read command carries a 2-byte address; reply once both bytes arrive.
    if (_byteCount >= 1)
    {
      _workingCmd = 0;
      _byteCount = 0;

      joybus_jump_output(PIO_IN_USE_N64, PIO_SM, _n64_offset);

      // End receive so we respond
      c = _delay_cycles_memread;
      while (c--)
        asm("nop");

      // 0x8000 is the pak identify region: answer "rumble pak", else return zeroed data.
      uint16_t readaddr = (_n64_hal_in_buffer[0] << 8) | (_n64_hal_in_buffer[1] & 0xE0);
      if (readaddr == 0x8000)
      {
        _n64_send_rumble_identify();
      }
      else
        _n64_send_null_identify();
    }
    else
      _byteCount++;
  }
  // Single byte commands and setup
  // for future handling
  else
  {
    _workingCmd = pio_sm_get(PIO_IN_USE_N64, PIO_SM);

    switch (_workingCmd)
    {
    default:
      break;

    // Read from mem pak
    case N64_CMD_READMEM:
      _crc_reply = 0;
      break;

    // Write to mem pak
    case N64_CMD_WRITEMEM:
      _crc_reply = 0;
      break;

    // Probe/Reset target response time 3us
    case N64_CMD_RESET:
    case N64_CMD_PROBE:
      c = _delay_cycles_probe;
      while (c--)
        asm("nop");
      joybus_jump_output(PIO_IN_USE_N64, PIO_SM, _n64_offset);
      _n64_send_probe();
      _workingCmd = 0;
      break;

    // Poll
    case N64_CMD_POLL:
      c = _delay_cycles_poll;
      while (c--)
        asm("nop");
      joybus_jump_output(PIO_IN_USE_N64, PIO_SM, _n64_offset);
      _n64_send_poll();
      _n64_sent_data = true;
      _workingCmd = 0;
      break;
    }
  }
}

/**
 * @brief PIO interrupt entry point; dispatches to the command handler.
 *
 * Masks the IRQ for the duration so the handler runs uninterrupted, clears the
 * PIO interrupt flag, and records that traffic was seen for the watchdog.
 */
static void __time_critical_func(_n64_isr_handler)(void)
{
  irq_set_enabled(_n64_irq, false);
  if (pio_interrupt_get(PIO_IN_USE_N64, 0))
  {
    pio_interrupt_clear(PIO_IN_USE_N64, 0);
    _n64_command_handler();
    _n64_got_data = true;
  }
  irq_set_enabled(_n64_irq, true);
}

/**
 * @brief Re-initialize the Joybus PIO program back to its receive-ready state.
 */
void _n64_reset_state()
{
  joybus_program_init(PIO_IN_USE_N64, PIO_SM, _n64_offset, JOYBUS_N64_DRIVER_DATA_PIN, &_n64_c);
}

/**
 * @brief Load the Joybus PIO program and arm its interrupt handler.
 *
 * @return true once the state machine is initialized and the IRQ is enabled.
 */
bool _joybus_n64_hal_init()
{
  _n64_offset = pio_add_program(PIO_IN_USE_N64, &joybus_program);

  _n64_irq = PIO_IRQ_USE_0;

  pio_set_irq0_source_enabled(PIO_IN_USE_N64, pis_interrupt0, true);

  irq_set_exclusive_handler(_n64_irq, _n64_isr_handler);

  irq_set_priority(PIO_IRQ_USE_0, 0);

  joybus_program_init(PIO_IN_USE_N64, PIO_SM, _n64_offset, JOYBUS_N64_DRIVER_DATA_PIN, &_n64_c);
  irq_set_enabled(_n64_irq, true);
  return true;
}

/**
 * @brief Forward the decoded rumble state to core0's rumble output.
 */
void _jb64_handle_rumble()
{
  // Handle rumble state if it changes
  static bool rumblestate = false;
  if (_n64_rumble != rumblestate)
  {
    rumblestate = _n64_rumble;
  }
  
  uint8_t rumble = rumblestate ? 255 : 0;

  core0_set_rumble(rumble, rumble, 0, 0);
}

/**
 * @brief Emit a connect/disconnect edge to core0 (player number + transport status).
 *
 * Only fires on a real state change. On disconnect it also re-inits the PIO and
 * pauses briefly to let the line settle.
 *
 * @param connected Current console-present state.
 */
void _jb64_handle_connection(bool connected)
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
    _n64_reset_state();
    sleep_ms(8);
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
 * Re-inits the PIO, seeds a neutral input snapshot so the first poll reply is
 * centered, signals disconnect, and resets wireless link pump timing.
 */
static void _jb64_reset()
{
  // Disable PIO IRQ to prevent races during re-init
  irq_set_enabled(_n64_irq, false);

  _n64_reset_state();

  core_n64_report_s neutral_report = {
      .stick_x = 0,
      .stick_y = 0,
      .buttons_1 = 0,
      .buttons_2 = 0,
  };

  // Ensure first poll response is neutral
  snapshot_n64input_write(&_n64_hal_snap, &neutral_report);

  _jb64_handle_connection(false);

  core1_link_pump_reset_timing();

  irq_set_enabled(_n64_irq, true);
}

/***********************************************/
/********* Transport Defines *******************/

/** @brief Stop the N64 transport: tear down PIO/IRQ and clear all HAL state. */
void transport_jb64_stop()
{
  // Disable the PIO IRQ first to prevent races
  irq_set_enabled(_n64_irq, false);

  // Remove the IRQ handler
  irq_remove_handler(_n64_irq, _n64_isr_handler);

  // Disable the PIO IRQ source
  pio_set_irq0_source_enabled(PIO_IN_USE_N64, pis_interrupt0, false);

  // Disable and clean up the state machine
  pio_sm_set_enabled(PIO_IN_USE_N64, PIO_SM, false);
  pio_sm_clear_fifos(PIO_IN_USE_N64, PIO_SM);

  // Remove the PIO program from instruction memory
  pio_remove_program(PIO_IN_USE_N64, &joybus_program, _n64_offset);

  // Reset internal state
  _workingCmd = 0x00;
  _byteCount = 0;
  _crc_reply = 0;
  _n64_got_data = false;
  _n64_sent_data = false;
  _n64_rumble = false;
  core1_link_pump_reset_timing();
  _n64_hal_params = NULL;

  // Notify disconnection
  _jb64_handle_connection(false);
}

/**
 * @brief Start the N64 transport for the given core parameters.
 *
 * Rejects cores whose report format is not N64, then brings up the HAL.
 *
 * @param params Core parameters (must use CORE_REPORTFORMAT_N64).
 * @return true if the transport was started, false on format mismatch.
 */
bool transport_jb64_init(core_params_s *params)
{
  if (params->core_report_format != CORE_REPORTFORMAT_N64)
    return false;

  _n64_hal_params = params;

  _joybus_n64_hal_init();
  return true;
}

/**
 * @brief Per-tick service for the N64 transport.
 *
 * Paces the wireless link pump off poll replies, refreshes the input snapshot
 * at the configured poll rate, services rumble, runs the 5 s comms-loss
 * watchdog, and reports connection on fresh traffic.
 *
 * @param timestamp Current time in microseconds.
 */
void transport_jb64_task(uint64_t timestamp)
{
  if (!_n64_hal_params)
    return;

  static interval_s interval = {0};
  static interval_s interval_reset = {0};
  static interval_s interval_rumble = {0};

  if (_n64_sent_data)
  {
    uint64_t now_us = timestamp;
    core1_link_pump_schedule_from_poll(now_us);
    core1_link_pump_mark_sent(now_us);
    _n64_sent_data = false;
  }

  // Input handling task
  if (interval_run(timestamp, _n64_hal_params->core_pollrate_us, &interval))
  {
    // Get input report here
    core_report_s report;
    if (core_get_generated_report(&report))
    {
      // Get input report here
      core_report_s report;
      if (core_get_generated_report(&report))
      {
        snapshot_n64input_write(&_n64_hal_snap, (core_n64_report_s *)report.data);
      }
    }
  }

  if(interval_run(timestamp, 32000, &interval_rumble))
  {
    // Rumblecore_gamecube_report_s
    _jb64_handle_rumble();
  }

  // Check for communication loss (5 second timeout)
  if (interval_resettable_run(timestamp, 5000000, _n64_got_data, &interval_reset))
  {
    _jb64_reset();
  }

  if (_n64_got_data)
  {
    _jb64_handle_connection(true);
    _n64_got_data = false;
  }
}
