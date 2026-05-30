/*
 * WS2812 addressable RGB LED HAL (PIO + DMA driven).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file rgb_hal.c
 * @brief PIO + DMA driver for a chain of WS2812 addressable LEDs.
 *
 * A WS2812 PIO program clocks the strict-timing bitstream out the data pin,
 * fed by a DMA channel sourced from an in-memory pixel buffer. Callers stage a
 * frame and trigger a non-blocking DMA burst, keeping LED refreshes off the
 * CPU's critical path.
 */

#include "hal/rgb_hal.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "generated/ws2812.pio.h"
#include "pico/stdlib.h"
#include <string.h>

#define RGB_DRIVER_OUTPUT_PIN 17
#define RGB_DRIVER_LED_COUNT HOJA_RGB_COUNT // Set as a power of 2 val, it can be larger than the strip

#define RGB_PIO_IN_USE pio2 

int _rgb_state_machine = 0;        /**< Claimed WS2812 PIO state machine index. */
int _rgb_dma_chan;                 /**< DMA channel feeding the PIO TX FIFO. */
uint32_t _rgb_states[RGB_DRIVER_LED_COUNT] = {0}; /**< Source frame buffer (one packed pixel per LED). */

void rgb_hal_init()
{
    // Claim a DMA channel and configure it to feed the PIO TX FIFO 32 bits at a
    // time, paced by the PIO's data request so it never overruns the shifter.
    _rgb_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(_rgb_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, pio_get_dreq(RGB_PIO_IN_USE, false, true));

    dma_channel_configure(
        _rgb_dma_chan,
        &c,
        &RGB_PIO_IN_USE->txf[0],
        &_rgb_states,
        RGB_DRIVER_LED_COUNT,
        false
    );

    // Load the WS2812 timing program onto a free state machine and blank the
    // strip with an initial all-zero frame.
    _rgb_state_machine = pio_claim_unused_sm(RGB_PIO_IN_USE, true);
    uint offset = pio_add_program(RGB_PIO_IN_USE, &ws2812_program);
    ws2812_program_init(RGB_PIO_IN_USE, (uint) _rgb_state_machine, offset, RGB_DRIVER_OUTPUT_PIN);
    rgb_hal_update(NULL);
}

void rgb_hal_deinit()
{
    
}


/**
 * @brief Stage a frame and start a DMA burst to the LED strip.
 *
 * Copies @p data into the persistent source buffer when provided, then triggers
 * a non-blocking DMA transfer of the full buffer to the PIO. Passing NULL
 * re-sends whatever is already buffered.
 */
void rgb_hal_update(const rgb_s *data)
{
    if(_rgb_state_machine < 0) return;

    if(data != NULL)
        memcpy(_rgb_states, data, sizeof(uint32_t) * RGB_DRIVER_LED_COUNT);

    dma_channel_transfer_from_buffer_now(_rgb_dma_chan, _rgb_states, RGB_DRIVER_LED_COUNT);
}
