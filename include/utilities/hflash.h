/*
 * Public interface for the deferred on-chip flash persistence helper.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file hflash.h
 * @brief Queued sector read/write API for the reserved flash region.
 *
 * Exposes the entry points for persisting and retrieving up to one flash
 * sector of data. Writes are queued and committed later from hflash_task();
 * reads return immediately.
 */

#ifndef HFLASH_H
#define HFLASH_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Queue a single-sector write to be committed by hflash_task().
 * @param data Source buffer to persist (must remain valid until committed).
 * @param size Number of bytes to write (must be <= FLASH_SECTOR_SIZE).
 * @param page Sector index, counted backwards from the reserved base.
 * @return true if queued; false if a write is already pending or size too large.
 */
bool hflash_write(uint8_t *data, uint32_t size, uint32_t page);

/**
 * @brief Read up to one sector from the reserved region into @p out.
 * @param out  Destination buffer.
 * @param size Number of bytes to read (must be <= FLASH_SECTOR_SIZE).
 * @param page Sector index, counted backwards from the reserved base.
 * @return true on success; false if size exceeds a sector.
 */
bool hflash_read(uint8_t *out, uint32_t size, uint32_t page);

/** @brief Service any pending queued write; call from a main loop. */
void hflash_task();

/** @brief Initialize the flash-safe execution helper (call once on core 0). */
void hflash_init();

#endif