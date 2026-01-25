#ifndef _MIA_CFG_H_
#define _MIA_CFG_H_

#include <stdint.h>

#include "pico.h"

uint8_t __not_in_flash_func(get_cfg)(uint8_t config_id);
void __not_in_flash_func(set_cfg)(uint8_t id, uint8_t value);

#endif