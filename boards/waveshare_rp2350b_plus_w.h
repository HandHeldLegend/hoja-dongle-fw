/*
 * Waveshare RP2350B-Plus-W: an RP2350B (QFN-80, 48 GPIOs) with 16 MB flash and the Raspberry Pi
 * Radio Module 2 (CYW43439), Pico 2 W-sized, USB-C.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

// The Pico SDK has no definition for this board (2.2.0). The radio sits on GPIO 36-39, pins only
// the RP2350B has, where the Pico 2 W uses 23, 24, 25 and 29: WL_ON 36, the shared data / host wake
// line 37, CS 38, clock 39 (the schematic's nets, and the board file in arduino-pico issue #3297).
// The board's red LED is GPIO 23; LED1 is the radio module's GPIO 0, as on the Pico W.

#ifndef _BOARDS_WAVESHARE_RP2350B_PLUS_W_H
#define _BOARDS_WAVESHARE_RP2350B_PLUS_W_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

#define WAVESHARE_RP2350B_PLUS_W

// The B package: 48 GPIOs
#define PICO_RP2350A 0

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 23
#endif

#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// 16 MB W25Q128JV
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// The radio module's own GPIOs
#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif
#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif

// VSYS is sensed on its own pin here, not shared with the radio's clock as on the Pico 2 W
#ifndef CYW43_USES_VSYS_PIN
#define CYW43_USES_VSYS_PIN 0
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// The radio's pins
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 36u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 38u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 39u
#endif

#endif
