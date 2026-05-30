/*
 * Dual RGB status LED driver and front-panel button handling for the dongle.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file rgb.c
 * @brief Mode/connection status LEDs and the buttons that toggle them.
 *
 * Drives two WS2812 LEDs: one encodes the active controller mode (solid when
 * the console transport is connected, blinking while idle) and one encodes the
 * wireless link state (solid orange when up, blinking blue while down). Two
 * front-panel buttons toggle their respective LED on a long hold, and holding
 * both at boot enters the USB UF2 bootloader. LED state is composed locally and
 * pushed to the hardware only when it changes.
 */

#include "utilities/rgb.h"

#include "hal/rgb_hal.h"
#include "core0transport.h"

#include <dongle.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#ifndef HOJA_RGB_COUNT
#define HOJA_RGB_COUNT 2
#endif

#ifndef HOJA_BTN_HOLD_US
#define HOJA_BTN_HOLD_US 500000u
#endif

#ifndef HOJA_BTN_BOOT_DEBOUNCE_MS
#define HOJA_BTN_BOOT_DEBOUNCE_MS 50u
#endif

/* Half-period of the status blink (LED toggles on/off every 500 ms). */
#define RGB_FLASH_PERIOD_US 500000u

/* Resolve which physical LED index is "mode" vs "connection". The two can be
 * physically swapped on the board, so HOJA_RGB_LED_SWAP flips the mapping. */
#if HOJA_RGB_LED_SWAP
#define RGB_IDX_MODE DONGLE_RGB_LED_CONN
#define RGB_IDX_CONN DONGLE_RGB_LED_MODE
#else
#define RGB_IDX_MODE DONGLE_RGB_LED_MODE
#define RGB_IDX_CONN DONGLE_RGB_LED_CONN
#endif

/* Abstract the active-low vs active-high wiring behind a single predicate. */
#if HOJA_BTN_ACTIVE_LOW
#define BTN_IS_PRESSED(gpio) (!gpio_get(gpio))
#else
#define BTN_IS_PRESSED(gpio) (gpio_get(gpio))
#endif

static uint8_t _mode = DONGLE_MODE_N64; /* Fallback only; core_init() sets the real boot mode */
static uint8_t _link;      /* dongle_link_status_t — wireless link */
static uint8_t _transport; /* dongle_transport_status_t — dongle->console */
static rgb_s _leds[HOJA_RGB_COUNT]; /* Composed LED state (logical) */
static rgb_s _tx[HOJA_RGB_COUNT];   /* Per-push buffer handed to the HAL */
static bool _mode_led_enabled = true;
static bool _conn_led_enabled = true;
static bool _flash_on;          /* shared blink phase for both LEDs */
static uint64_t _flash_next_us; /* Next time the blink phase flips */
static bool _dirty;             /* Set when _leds changed and needs pushing */

typedef struct
{
    uint gpio;             /**< Button input GPIO */
    bool *led_enabled;     /**< LED-enable flag this button toggles */
    bool was_pressed;      /**< Press state from the previous task tick */
    bool hold_fired;       /**< True once the long-hold action has run */
    uint64_t press_start_us; /**< Timestamp of the current press start */
} dongle_rgb_btn_t;

static dongle_rgb_btn_t _btn1; /* BTN1 (GPIO20): mode LED */
static dongle_rgb_btn_t _btn2; /* BTN2 (GPIO29): WLAN / connection LED */

/** @brief Scale a single 0–255 channel value by the global brightness. */
static uint8_t _scale_channel(uint8_t v)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)HOJA_RGB_BRIGHTNESS) / 255u);
}

/** @brief Write a brightness-scaled RGB triplet into @p out. */
static void _set_rgb(rgb_s *out, uint8_t r, uint8_t g, uint8_t b)
{
    out->r = _scale_channel(r);
    out->g = _scale_channel(g);
    out->b = _scale_channel(b);
}

/** @brief Force an LED to off (all channels zero). */
static void _set_rgb_off(rgb_s *out)
{
    out->r = 0;
    out->g = 0;
    out->b = 0;
}

/** @brief Map a dongle mode to its brightness-scaled signature color. */
static void _mode_color(uint8_t mode, rgb_s *out)
{
    switch ((dongle_mode_t)mode)
    {
    case DONGLE_MODE_XINPUT:
        _set_rgb(out, 0, 255, 0);
        break;
    case DONGLE_MODE_SINPUT:
        _set_rgb(out, 0, 0, 255);
        break;
    case DONGLE_MODE_SLIPPI:
        _set_rgb(out, 0, 255, 255);
        break;
    case DONGLE_MODE_SWITCH:
        _set_rgb(out, 255, 255, 255);
        break;
    case DONGLE_MODE_N64:
        _set_rgb(out, 255, 255, 0);
        break;
    case DONGLE_MODE_GAMECUBE:
        _set_rgb(out, 180, 0, 255);
        break;
    case DONGLE_MODE_SNES:
        _set_rgb(out, 255, 0, 0);
        break;
    default:
        _set_rgb(out, 32, 32, 32);
        break;
    }
}

/** @brief Recompute the mode LED color from mode/transport/blink state. */
static void _refresh_mode_led(void)
{
    if (!_mode_led_enabled)
    {
        _set_rgb_off(&_leds[RGB_IDX_MODE]);
        return;
    }

    /* Solid mode color once the console transport is connected; blink while idle. */
    if (_transport == DONGLE_TRANSPORT_CONNECTED || _flash_on)
    {
        _mode_color(_mode, &_leds[RGB_IDX_MODE]);
    }
    else
    {
        _set_rgb_off(&_leds[RGB_IDX_MODE]);
    }
}

/** @brief Recompute the connection LED color from link/blink state. */
static void _refresh_conn_led(void)
{
    if (!_conn_led_enabled)
    {
        _set_rgb_off(&_leds[RGB_IDX_CONN]);
        return;
    }

    /* Solid orange when the wireless link is up; blink blue while down. */
    if (_link == DONGLE_LINK_UP)
    {
        _set_rgb(&_leds[RGB_IDX_CONN], 255, 80, 0);
        return;
    }

    if (_flash_on)
    {
        _set_rgb(&_leds[RGB_IDX_CONN], 0, 0, 255);
    }
    else
    {
        _set_rgb_off(&_leds[RGB_IDX_CONN]);
    }
}

/** @brief Copy the composed LED state into the TX buffer, masking disabled LEDs. */
static void _compose_tx_buffer(void)
{
    memcpy(_tx, _leds, sizeof(_tx));
    if (!_mode_led_enabled)
    {
        _set_rgb_off(&_tx[RGB_IDX_MODE]);
    }
    if (!_conn_led_enabled)
    {
        _set_rgb_off(&_tx[RGB_IDX_CONN]);
    }
}

/** @brief Compose and unconditionally push the current LED state to the HAL. */
static void _push_now(void)
{
    _compose_tx_buffer();
    rgb_hal_update(_tx);
}

/** @brief Push to the HAL only if the composed state changed since last push. */
static void _push_if_dirty(void)
{
    if (!_dirty)
    {
        return;
    }
    _dirty = false;
    _push_now();
}

/** @brief Configure a button GPIO as input with the pull matching the wiring. */
static void _buttons_init_gpio(uint gpio)
{
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
#if HOJA_BTN_ACTIVE_LOW
    gpio_pull_up(gpio);
#else
    gpio_pull_down(gpio);
#endif
}

/* Initialize both front-panel button GPIOs. */
void dongle_rgb_gpio_init(void)
{
    _buttons_init_gpio(HOJA_BTN1_PIN);
    _buttons_init_gpio(HOJA_BTN2_PIN);
}

/** @brief True only while both front-panel buttons are held simultaneously. */
static bool _both_buttons_pressed(void)
{
    return BTN_IS_PRESSED(HOJA_BTN1_PIN) && BTN_IS_PRESSED(HOJA_BTN2_PIN);
}

/* Boot-time bootloader entry: debounce briefly, then jump to USB UF2 boot if
 * both buttons are held. reset_usb_boot() does not return on a successful jump. */
void dongle_rgb_enter_bootloader_if_buttons_held(void)
{
    dongle_rgb_gpio_init();
    sleep_ms(HOJA_BTN_BOOT_DEBOUNCE_MS);
    if (_both_buttons_pressed())
    {
        reset_usb_boot(0, 0);
    }
}

/**
 * @brief Edge/hold state machine for one button.
 *
 * Latches the press start on the rising edge, then once the press has lasted
 * HOJA_BTN_HOLD_US it fires once: toggles the button's LED-enable flag,
 * refreshes both LEDs, and pushes immediately. hold_fired prevents repeated
 * toggling while the button stays held.
 */
static void _button_task(dongle_rgb_btn_t *btn, uint64_t now_us)
{
    bool pressed = BTN_IS_PRESSED(btn->gpio);

    if (pressed)
    {
        if (!btn->was_pressed)
        {
            btn->was_pressed = true;
            btn->hold_fired = false;
            btn->press_start_us = now_us;
        }
        else if (!btn->hold_fired && (now_us - btn->press_start_us) >= HOJA_BTN_HOLD_US)
        {
            *(btn->led_enabled) = !*(btn->led_enabled);
            btn->hold_fired = true;
            _refresh_mode_led();
            _refresh_conn_led();
            _push_now();
        }
    }
    else
    {
        btn->was_pressed = false;
        btn->hold_fired = false;
    }
}

/** @brief Run the hold state machine for both buttons. */
static void _buttons_task(uint64_t now_us)
{
    _button_task(&_btn1, now_us);
    _button_task(&_btn2, now_us);
}

/* Bring up the LED HAL and buttons, seed default state, and push the first frame. */
void dongle_rgb_init(void)
{
    rgb_hal_init();

    dongle_rgb_gpio_init();

    _btn1 = (dongle_rgb_btn_t){
        .gpio = HOJA_BTN1_PIN,
        .led_enabled = &_mode_led_enabled,
    };
    _btn2 = (dongle_rgb_btn_t){
        .gpio = HOJA_BTN2_PIN,
        .led_enabled = &_conn_led_enabled,
    };

    /* Keep the mode core_init() already selected (this runs after core_init); only
     * seed link/transport/blink state here. */
    _link = DONGLE_LINK_DOWN;
    _transport = DONGLE_TRANSPORT_IDLE;
    _flash_on = true;
    _flash_next_us = 0;
    memset(_leds, 0, sizeof(_leds));
    memset(_tx, 0, sizeof(_tx));
    _dirty = true;
    _refresh_mode_led();
    _refresh_conn_led();
    _push_if_dirty();
}

/* Update the active mode and mark the mode LED dirty (no-op if unchanged). */
void dongle_rgb_set_mode(uint8_t dongle_mode)
{
    if (_mode == dongle_mode)
    {
        return;
    }
    _mode = dongle_mode;
    _refresh_mode_led();
    _dirty = true;
}

/* Periodic task: service buttons, pull the latest status snapshot, advance the
 * blink phase when anything is blinking, and push the LEDs only when changed. */
void dongle_rgb_task(uint64_t now_us)
{
    _buttons_task(now_us);

    dongle_status_u status;
    core0_get_status(&status);

    bool refresh = false;

    if (status.link_status != _link)
    {
        _link = status.link_status;
        refresh = true;
    }

    if (status.transport_status != _transport)
    {
        _transport = status.transport_status;
        refresh = true;
    }

    /* Mode LED blinks while the console transport is idle; conn LED blinks while
     * the wireless link is down. Advance the shared phase if either is blinking. */
    bool blinking = (_transport != DONGLE_TRANSPORT_CONNECTED) || (_link != DONGLE_LINK_UP);
    if (blinking && now_us >= _flash_next_us)
    {
        _flash_on = !_flash_on;
        _flash_next_us = now_us + RGB_FLASH_PERIOD_US;
        refresh = true;
    }

    if (refresh)
    {
        _refresh_mode_led();
        _refresh_conn_led();
        _dirty = true;
    }

    _push_if_dirty();
}
