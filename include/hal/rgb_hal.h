#ifndef HOJA_RGB_HAL_H
#define HOJA_RGB_HAL_H

#include <stdint.h>
#include <hoja_types.h>

#ifndef HOJA_RGB_COUNT
#define HOJA_RGB_COUNT 32
#endif

void rgb_hal_init(void);
void rgb_hal_deinit(void);

/** Push count pixels (GRB) to the strip; count must not exceed HOJA_RGB_COUNT. */
void rgb_hal_update(const rgb_s *data);

#endif