/*
 * Public interface for the non-blocking fixed-interval timing helpers.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file interval.h
 * @brief Lightweight interval timers for pacing cooperative loop work.
 *
 * Declares the interval_s state and the helpers that report when a configured
 * microsecond interval has elapsed, so callers can throttle periodic work
 * without sleeping.
 */

#ifndef UTILITIES_INTERVAL_H
#define UTILITIES_INTERVAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
  uint64_t this_time; /**< Timestamp from the most recent call */
  uint64_t last_time; /**< Timestamp the interval was last rearmed */
} interval_s;

/**
 * @brief Report whether @p interval microseconds have elapsed and rearm if so.
 * @param timestamp Current time in microseconds.
 * @param interval  Target interval in microseconds.
 * @param state     Caller-owned timer state.
 * @return true once the interval has elapsed (deadline then advances), else false.
 */
bool interval_run(uint64_t timestamp, uint32_t interval, interval_s *state);

/**
 * @brief Interval check with an explicit reset option.
 * @param timestamp Current time in microseconds.
 * @param interval  Target interval in microseconds.
 * @param reset     If true, rearm the timer from now and return false.
 * @param state     Caller-owned timer state.
 * @return true once the interval has elapsed (when not resetting), else false.
 */
bool interval_resettable_run(uint64_t timestamp, uint32_t interval, bool reset, interval_s *state);

#ifdef __cplusplus
}
#endif

#endif