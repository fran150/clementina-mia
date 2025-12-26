#ifndef PHI2_CLOCK_H
#define PHI2_CLOCK_H

#include "hardware/pio.h"
#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "hardware/gpio_mapping.h"


void phi2_clock_init(uint32_t freq_hz);

#endif