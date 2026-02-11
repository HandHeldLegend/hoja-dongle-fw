#ifndef TRANSPORT_JBGC_H
#define TRANSPORT_JBGC_H

#include <stdbool.h>
#include <stdint.h>

#include "cores/cores.h"

void transport_jbgc_stop();
bool transport_jbgc_init(core_params_s *params);
void transport_jbgc_task(uint64_t timestamp);

#endif