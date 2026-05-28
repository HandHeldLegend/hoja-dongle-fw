#ifndef CORE_SWITCH_H
#define CORE_SWITCH_H

#include <stdbool.h>
#include "cores/cores.h"
#include <dongle.h>

bool core_switch_init(core_params_s *params, const dongle_wake_s *wake);

#endif
