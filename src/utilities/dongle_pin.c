/*
 * WLAN PIN persistence for the dongle access point.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "utilities/dongle_pin.h"
#include "utilities/hflash.h"

#include <dongle.h>

#include <stdbool.h>
#include <string.h>

#define DONGLE_PIN_STORE_MAGIC 0x484F4A50u /* "HOJP" */
#define DONGLE_PIN_FLASH_PAGE  0u

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;
    uint8_t pin[4];
} dongle_pin_store_t;
#pragma pack(pop)

static uint8_t _pin[4];
static dongle_pin_store_t _store_buf;

static bool _pin_digits_valid(const uint8_t pin[4])
{
    for (uint8_t i = 0; i < 4u; i++)
    {
        if (pin[i] > 9u)
        {
            return false;
        }
    }
    return true;
}

static void _pin_generate(uint8_t pin[4])
{
    uint16_t key = dongle_api_hook_get_rand_u16() % 10000u;
    dongle_wlan_pin_from_u16(key, pin);
}

static void _pin_persist(void)
{
    dongle_pin_store_t store = {
        .magic = DONGLE_PIN_STORE_MAGIC,
    };
    memcpy(store.pin, _pin, sizeof(_pin));

    memcpy(&_store_buf, &store, sizeof(store));
    hflash_write((uint8_t *)&_store_buf, sizeof(store), DONGLE_PIN_FLASH_PAGE);
}

static void _pin_flush(void)
{
    while (hflash_pending())
    {
        hflash_task();
    }
}

void dongle_pin_init(void)
{
    dongle_pin_store_t store = {0};
    hflash_read((uint8_t *)&store, sizeof(store), DONGLE_PIN_FLASH_PAGE);

    if (store.magic == DONGLE_PIN_STORE_MAGIC && _pin_digits_valid(store.pin))
    {
        memcpy(_pin, store.pin, sizeof(_pin));
        return;
    }

    _pin_generate(_pin);
    _pin_persist();
    /* Flash commit is deferred to hflash_task() once both cores are ready. */
}

void dongle_pin_get(uint8_t pin[4])
{
    if (pin != NULL)
    {
        memcpy(pin, _pin, sizeof(_pin));
    }
}

void dongle_pin_regenerate_and_save(void)
{
    _pin_generate(_pin);
    _pin_persist();
}

void dongle_pin_flush_save(void)
{
    _pin_flush();
}
