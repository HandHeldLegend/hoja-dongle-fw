#ifndef DONGLE_RGB_H
#define DONGLE_RGB_H

#include <stdint.h>

/**
 * WS2812 brightness scale (0–255). Applied to all channel values before TX.
 * Override at build time: -DHOJA_RGB_BRIGHTNESS=32
 */
#ifndef HOJA_RGB_BRIGHTNESS
#define HOJA_RGB_BRIGHTNESS 48u
#endif

/** Hold time before a button toggles its LED (microseconds). */
#ifndef HOJA_BTN_HOLD_US
#define HOJA_BTN_HOLD_US 500000u
#endif

#ifndef HOJA_BTN1_PIN
#define HOJA_BTN1_PIN 20
#endif

#ifndef HOJA_BTN2_PIN
#define HOJA_BTN2_PIN 29
#endif

/** 1 = pressed reads low (button to GND). 0 = pressed reads high. */
#ifndef HOJA_BTN_ACTIVE_LOW
#define HOJA_BTN_ACTIVE_LOW 1
#endif

/** Set to 1 if the LED closest to the MCU is connection, not mode. */
#ifndef HOJA_RGB_LED_SWAP
#define HOJA_RGB_LED_SWAP 0
#endif

/** BTN1 (GPIO20): mode LED. BTN2 (GPIO29): WLAN / connection LED. */
#define DONGLE_RGB_LED_MODE 0u
#define DONGLE_RGB_LED_CONN 1u

void dongle_rgb_gpio_init(void);

void dongle_rgb_init(void);
void dongle_rgb_set_mode(uint8_t dongle_mode);
void dongle_rgb_task(uint64_t now_us);

/** Call once at boot; if BTN1+BTN2 are held, enters USB UF2 bootloader (noreturn). */
void dongle_rgb_enter_bootloader_if_buttons_held(void);

#endif
