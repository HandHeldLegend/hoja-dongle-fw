/*
 * Deferred, flash-safe single-sector persistence helper for the RP2040.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file hflash.c
 * @brief Queued read/write access to a reserved on-chip flash sector.
 *
 * Provides a tiny persistence layer that reserves a flash sector just ahead of
 * BTstack's flash-bank storage. Writes are queued by hflash_write() and later
 * committed from hflash_task() via the Pico SDK's flash-safe execution path,
 * so callers (including callbacks) never block on flash erase/program. Reads
 * are immediate through the memory-mapped XIP alias.
 */

#include <stdint.h>
#include <string.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/structs/xip_ctrl.h"

/*
 * Reserve one sector immediately ahead of BTstack's flash-bank storage so the
 * example can persist its own pairing structure without colliding with BTstack.
 */
#define PICO_FLASH_BANK_TOTAL_SIZE (FLASH_SECTOR_SIZE * 2u)
#define PICO_FLASH_BANK_STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - PICO_FLASH_BANK_TOTAL_SIZE)
#define FLASH_START_OFFSET (PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE)

/* Flash writes are queued so callers can request persistence from callbacks without blocking there. */
volatile bool _flash_go = false;
uint8_t *_write_from = NULL;
volatile uint32_t _write_size = 0;
volatile uint32_t _write_offset = 0;

/** @brief XIP (memory-mapped) read address for the sector @p pages below the base. */
uint32_t _get_sector_offset_read(uint32_t page)
{
    /* XIP reads use an absolute memory-mapped address. */
    uint32_t target_offset = XIP_BASE + FLASH_START_OFFSET - (page * FLASH_SECTOR_SIZE);
    return target_offset;
}

/** @brief Raw flash offset (erase/program) for the sector @p pages below the base. */
uint32_t _get_sector_offset_write(uint32_t page)
{
    /* Erase/program operations use the raw flash offset instead of the XIP alias. */
    uint32_t target_offset = FLASH_START_OFFSET - (page * FLASH_SECTOR_SIZE);
    return target_offset;
}

/* Queue a single-sector write request for later execution from hflash_task(). */
bool hflash_write(uint8_t *data, uint32_t size, uint32_t page) 
{
    if(_flash_go) return false;
    if(size > FLASH_SECTOR_SIZE) return false;

    _write_from = data;
    _write_size = size;
    _write_offset = _get_sector_offset_write(page);
    _flash_go = true;

    return true;
}

/**
 * @brief Flash-safe callback that commits the queued write.
 *
 * Runs under flash_safe_execute() with the other core paused and XIP disabled.
 */
void _flash_safe_write(void * params)
{
    (void)params;

    /*
     * The RP2040 can only program erased flash, so rebuild a full sector image
     * in RAM, erase the target sector, then program the completed sector back.
     */
    uint8_t thisPage[FLASH_SECTOR_SIZE] = {0x00};
    memcpy(thisPage, _write_from, _write_size);
    flash_range_erase(_write_offset, FLASH_SECTOR_SIZE);
    flash_range_program(_write_offset, thisPage, FLASH_SECTOR_SIZE);
}

/* Immediate read of up to one sector from the reserved region via the XIP alias. */
bool hflash_read(uint8_t *out, uint32_t size, uint32_t page) 
{
    if(size > FLASH_SECTOR_SIZE) return false;

    uint32_t offset = _get_sector_offset_read(page);
    const uint8_t *flash_target_contents = (const uint8_t *) (offset);
    memcpy(out, flash_target_contents, size);
    return true;
}

/* Initialize Pico's flash-safe execution helpers before any queued writes run. */
void hflash_init()
{
    uint core = get_core_num();
    if(core==0)
    {
        flash_safe_execute_core_init();
    }
}

/* Service pending writes from the main transport loops. */
void hflash_task()
{
    if(_flash_go)
    {
        flash_safe_execute(_flash_safe_write, NULL, UINT32_MAX);

        _write_from = NULL;
        _write_size = 0;
        _write_offset = 0;

        _flash_go = false;
    }
}
