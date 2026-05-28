#ifndef HFLASH_H
#define HFLASH_H

#include <stdint.h>
#include <stdbool.h>

bool hflash_write(uint8_t *data, uint32_t size, uint32_t page);
bool hflash_read(uint8_t *out, uint32_t size, uint32_t page);
void hflash_task();
void hflash_init();

#endif