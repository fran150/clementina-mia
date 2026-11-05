#include "err.h"

// Allocation of the volatile variables
volatile uint8_t _err_first = 0;
volatile uint8_t _err_last = 0;
volatile uint8_t _err_buf[16];
atomic_uint _err_deferred_flags;
volatile uint16_t mia_error_word = 0;
volatile uint16_t mia_status_word = 0;

void error_service(void) {
    uint32_t flags = atomic_exchange(&_err_deferred_flags, 0);

    if ((flags & ERROR_DEFER_CMD_QUEUE_FULL) != 0) {
        error_push(ERROR_CMD_QUEUE_FULL);
    }

    if ((flags & ERROR_DEFER_AUDIO_QUEUE_OVERFLOW) != 0) {
        error_push(ERROR_AUDIO_QUEUE_OVERFLOW);
    }

    if ((flags & ERROR_DEFER_CTX_OVERFLOW) != 0) {
        error_push(ERROR_CTX_OVERFLOW);
    }

    if ((flags & ERROR_DEFER_CTX_UNDERFLOW) != 0) {
        error_push(ERROR_CTX_UNDERFLOW);
    }
}
