#include "hal/dongle_rgb.h"

#include "hal/rgb_hal.h"
#include "hdongle.h"

#include <dongle.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#ifndef HOJA_RGB_COUNT
#define HOJA_RGB_COUNT 32
#endif

#ifndef HOJA_BTN_HOLD_US
#define HOJA_BTN_HOLD_US 500000u
#endif

#ifndef HOJA_BTN_BOOT_DEBOUNCE_MS
#define HOJA_BTN_BOOT_DEBOUNCE_MS 50u
#endif

#define RGB_FLASH_PERIOD_US 500000u

#if HOJA_RGB_LED_SWAP
#define RGB_IDX_MODE DONGLE_RGB_LED_CONN
#define RGB_IDX_CONN DONGLE_RGB_LED_MODE
#else
#define RGB_IDX_MODE DONGLE_RGB_LED_MODE
#define RGB_IDX_CONN DONGLE_RGB_LED_CONN
#endif

#if HOJA_BTN_ACTIVE_LOW
#define BTN_IS_PRESSED(gpio) (!gpio_get(gpio))
#else
#define BTN_IS_PRESSED(gpio) (gpio_get(gpio))
#endif

static uint8_t _mode;
static dongle_connection_t _conn;
static rgb_s _leds[HOJA_RGB_COUNT];
static rgb_s _tx[HOJA_RGB_COUNT];
static bool _mode_led_enabled = true;
static bool _conn_led_enabled = true;
static bool _conn_phase_on;
static uint64_t _conn_phase_next_us;
static bool _dirty;

typedef struct
{
    uint gpio;
    bool *led_enabled;
    bool was_pressed;
    bool hold_fired;
    uint64_t press_start_us;
} dongle_rgb_btn_t;

static dongle_rgb_btn_t _btn1; /* BTN1: WLAN / connection LED */
static dongle_rgb_btn_t _btn2; /* BTN2: mode LED */

static uint8_t _scale_channel(uint8_t v)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)HOJA_RGB_BRIGHTNESS) / 255u);
}

static void _set_rgb(rgb_s *out, uint8_t r, uint8_t g, uint8_t b)
{
    out->r = _scale_channel(r);
    out->g = _scale_channel(g);
    out->b = _scale_channel(b);
}

static void _set_rgb_off(rgb_s *out)
{
    out->r = 0;
    out->g = 0;
    out->b = 0;
}

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

static void _refresh_mode_led(void)
{
    if (_mode_led_enabled)
    {
        _mode_color(_mode, &_leds[RGB_IDX_MODE]);
    }
    else
    {
        _set_rgb_off(&_leds[RGB_IDX_MODE]);
    }
}

static void _refresh_conn_led(dongle_connection_t conn, bool flash_on)
{
    if (!_conn_led_enabled)
    {
        _set_rgb_off(&_leds[RGB_IDX_CONN]);
        return;
    }

    if (conn == DONGLE_CONN_CONNECTED)
    {
        _set_rgb(&_leds[RGB_IDX_CONN], 255, 80, 0);
        return;
    }

    if (flash_on)
    {
        _set_rgb(&_leds[RGB_IDX_CONN], 0, 0, 255);
    }
    else
    {
        _set_rgb_off(&_leds[RGB_IDX_CONN]);
    }
}

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

static void _push_now(void)
{
    _compose_tx_buffer();
    rgb_hal_update(_tx);
}

static void _push_if_dirty(void)
{
    if (!_dirty)
    {
        return;
    }
    _dirty = false;
    _push_now();
}

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

void dongle_rgb_gpio_init(void)
{
    _buttons_init_gpio(HOJA_BTN1_PIN);
    _buttons_init_gpio(HOJA_BTN2_PIN);
}

static bool _both_buttons_pressed(void)
{
    return BTN_IS_PRESSED(HOJA_BTN1_PIN) && BTN_IS_PRESSED(HOJA_BTN2_PIN);
}

void dongle_rgb_enter_bootloader_if_buttons_held(void)
{
    dongle_rgb_gpio_init();
    sleep_ms(HOJA_BTN_BOOT_DEBOUNCE_MS);
    if (_both_buttons_pressed())
    {
        reset_usb_boot(0, 0);
    }
}

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
            _refresh_conn_led(_conn, _conn_phase_on);
            _push_now();
        }
    }
    else
    {
        btn->was_pressed = false;
        btn->hold_fired = false;
    }
}

static void _buttons_task(uint64_t now_us)
{
    _button_task(&_btn1, now_us);
    _button_task(&_btn2, now_us);
}

void dongle_rgb_init(void)
{
    rgb_hal_init();

    dongle_rgb_gpio_init();

    _btn1 = (dongle_rgb_btn_t){
        .gpio = HOJA_BTN1_PIN,
        .led_enabled = &_conn_led_enabled,
    };
    _btn2 = (dongle_rgb_btn_t){
        .gpio = HOJA_BTN2_PIN,
        .led_enabled = &_mode_led_enabled,
    };

    _mode = DONGLE_MODE_N64;
    _conn = DONGLE_CONN_IDLE;
    _conn_phase_on = true;
    _conn_phase_next_us = 0;
    memset(_leds, 0, sizeof(_leds));
    memset(_tx, 0, sizeof(_tx));
    _dirty = true;
    _refresh_mode_led();
    _refresh_conn_led(DONGLE_CONN_IDLE, true);
    _push_if_dirty();
}

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

void dongle_rgb_task(uint64_t now_us)
{
    _buttons_task(now_us);

    dongle_connection_t conn = (dongle_connection_t)hdongle_current_status()->connection;

    if (conn != _conn)
    {
        _conn = conn;
        _conn_phase_on = (conn == DONGLE_CONN_CONNECTED);
        _conn_phase_next_us = now_us;
        _refresh_conn_led(conn, _conn_phase_on);
        _dirty = true;
    }

    if (_conn_led_enabled && conn != DONGLE_CONN_CONNECTED && now_us >= _conn_phase_next_us)
    {
        _conn_phase_on = !_conn_phase_on;
        _conn_phase_next_us = now_us + RGB_FLASH_PERIOD_US;
        _refresh_conn_led(conn, _conn_phase_on);
        _dirty = true;
    }

    _push_if_dirty();
}
