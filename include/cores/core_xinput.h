#ifndef CORE_XINPUT_H
#define CORE_XINPUT_H

#include <stdbool.h>
#include "cores/cores.h"
#include <dongle.h>

bool core_xinput_init(core_params_s *params, const dongle_wake_s *wake);

#endif
