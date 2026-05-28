/*
 * Non-blocking fixed-interval timing helper.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file interval.c
 * @brief Edge-style interval timers built on a caller-supplied timestamp.
 *
 * These helpers let the cooperative main loops run work at a target rate
 * without sleeping: the caller passes the current time each tick and the
 * function reports whether the configured interval has elapsed, advancing its
 * internal deadline when it has.
 */

#include "utilities/interval.h"

/* Returns true once `interval` microseconds have elapsed since the last fire,
 * then rearms from the current timestamp. Lets the caller pace work at a target
 * rate without blocking. */
bool interval_run(uint64_t timestamp, uint32_t interval, interval_s *state)
{
  state->this_time = timestamp;

  // Clear variable
  uint64_t diff = 0;

  diff = state->this_time - state->last_time;
  // We want a target rate according to our variable
  if (diff >= interval)
  {
    // Set the last time
    state->last_time = state->this_time;
    return true;
  }
  return false;
}

/* Same as interval_run(), but when `reset` is true the deadline is rearmed from
 * now and false is returned immediately, suppressing the current fire. */
bool interval_resettable_run(uint64_t timestamp, uint32_t interval, bool reset, interval_s *state)
{

  state->this_time = timestamp;

  if (reset)
  {
    state->last_time = state->this_time;
    return false;
  }

  // Clear variable
  uint64_t diff = 0;

  diff = state->this_time - state->last_time;
  // We want a target rate according to our variable
  if (diff >= interval)
  {
    // Set the last time
    state->last_time = state->this_time;
    return true;
  }
  return false;
}