#ifndef HOJA_RGB_HAL_H
#define HOJA_RGB_HAL_H

#include <stdint.h>
#include <hoja_types.h>

void rgb_hal_init();

void rgb_hal_deinit();

// Update all RGBs
void rgb_hal_update(rgb_s *data);

#endif