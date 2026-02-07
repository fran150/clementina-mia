#include "err.h"

// Allocation of the volatile variables
volatile uint8_t _err_first = 0;
volatile uint8_t _err_last = 0;
volatile uint8_t _err_buf[16];