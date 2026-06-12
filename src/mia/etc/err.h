#ifndef _MIA_ETC_ERR_H_
#define _MIA_ETC_ERR_H_

#include <stdint.h>
#include <stdbool.h>
#include "status.h"
#include "irq/irq.h"
#include "mem/regs.h"

#define ERROR_MIA_CANNOT_ALLOCATE_RAM   0x01        // MIA was not able to allocate enough ram on startup
#define ERROR_DMA_SIZE_ZERO             0x10        // Error when triggering DMA transfer with count in zero
#define ERROR_DMA_SRC_WILL_OVERFLOW     0x11        // Error when DMA + count of the source will overflow the memory max size
#define ERROR_DMA_TGT_WILL_OVERFLOW     0x12        // Error when DMA + count of the target will overflow the memory max size

extern volatile uint8_t _err_first;
extern volatile uint8_t _err_last;
extern volatile uint8_t _err_buf[16];

// Clears the error queue, the CPU-visible register, and its status bit.
static inline __force_inline void error_reset(void) {
    _err_first = 0;
    _err_last = 0;
    mia_regs->mia_error = 0;
    mia_status_clear_flag(MIA_STAT_ERRORS);
}

// Pushes the error to the queue. When the queue was empty the value is also
// preloaded into the CPU-visible MIA_ERROR register, so the very first read
// returns this error rather than a stale value (the read handler only advances
// to the *next* error). Producer side; runs on the core that detects the error.
static inline __force_inline void error_push(uint8_t error) {
    uint8_t next = (_err_last + 1) & 15;
    if (next != _err_first) {
        bool was_empty = (_err_first == _err_last);
        _err_buf[_err_last] = error;
        _err_last = next;
        mia_status_set_flag(MIA_STAT_ERRORS);
        mia_irq_set_flag(IRQ_ERROR);
        if (was_empty) {
            mia_regs->mia_error = error;
        }
    }
}

// Called after the 6502 has read the current head error from MIA_ERROR. Advances
// the queue and loads the next error (or 0 when drained) into the register so the
// following read returns it. Returns the value loaded. Consumer side.
static inline __force_inline uint8_t error_consume(void) {
    if (_err_first != _err_last) {
        _err_first = (_err_first + 1) & 15;
    }

    uint8_t next;
    if (_err_first == _err_last) {
        mia_status_clear_flag(MIA_STAT_ERRORS);
        next = 0;
    } else {
        next = _err_buf[_err_first];
    }

    mia_regs->mia_error = next;
    return next;
}

#endif
