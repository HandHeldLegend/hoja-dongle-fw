#ifndef HOJA_JOYBUS_GC_HAL_H
#define HOJA_JOYBUS_GC_HAL_H

#include <stdint.h>
#include <stdbool.h>

void joybus_gc_hal_stop();
bool joybus_gc_hal_init();
void joybus_gc_hal_task(uint64_t timestamp);

#endif