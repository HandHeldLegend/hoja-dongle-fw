#include "hal/rgb_hal.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "generated/ws2812.pio.h"

#define RGB_PIO_IN_USE pio0
#define RGB_DRIVER_LED_COUNT 8

#if !defined(RGB_DRIVER_OUTPUT_PIN)
#define RGB_DRIVER_OUTPUT_PIN 2
#endif

int _rgb_state_machine = 0;
int _rgb_dma_chan;
uint32_t _rgb_states[RGB_DRIVER_LED_COUNT] = {0};

void rgb_hal_init()
{
    // Init DMA channel
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

    _rgb_state_machine = pio_claim_unused_sm(RGB_PIO_IN_USE, true);
    uint offset = pio_add_program(RGB_PIO_IN_USE, &ws2812_program);
    ws2812_program_init(RGB_PIO_IN_USE, (uint) _rgb_state_machine, offset, RGB_DRIVER_OUTPUT_PIN);
    rgb_hal_update(NULL);
}

void rgb_hal_deinit()
{
    
}

// Update all RGBs
void rgb_hal_update(rgb_s *data)
{
    if(_rgb_state_machine < 0) return;

    if(data != NULL)
        memcpy(_rgb_states, data, sizeof(uint32_t) * RGB_DRIVER_LED_COUNT);

    dma_channel_transfer_from_buffer_now(_rgb_dma_chan, _rgb_states, RGB_DRIVER_LED_COUNT);
}
