#ifndef HOJA_JOYBUS_N64_HAL_H
#define HOJA_JOYBUS_N64_HAL_H

#include <stdint.h>

#define HOJA_JOYBUS_N64_TASK(timestamp) joybus_n64_hal_task(timestamp)
#define HOJA_JOYBUS_N64_INIT() joybus_n64_hal_init()

void joybus_n64_hal_stop();
bool joybus_n64_hal_init();
void joybus_n64_hal_task(uint64_t timestamp);

#endif