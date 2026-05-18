#ifndef _MIA_ETC_ERR_H_
#define _MIA_ETC_ERR_H_

#include <stdint.h>
#include "status.h"
#include "irq/irq.h"

#define ERROR_MIA_CANNOT_ALLOCATE_RAM   0x01        // MIA was not able to allocate enough ram on startup
#define ERROR_DMA_SIZE_ZERO             0x10        // Error when triggering DMA transfer with count in zero
#define ERROR_DMA_SRC_WILL_OVERFLOW     0x11        // Error when DMA + count of the source will overflow the memory max size
#define ERROR_DMA_TGT_WILL_OVERFLOW     0x12        // Error when DMA + count of the target will overflow the memory max size

extern volatile uint8_t _err_first;
extern volatile uint8_t _err_last;
extern volatile uint8_t _err_buf[16];

// Clears the error queue and its visible status bit.
static inline __force_inline void error_reset(void) {
    _err_first = 0;
    _err_last = 0;
    mia_status_clear_flag(MIA_STAT_ERRORS);
}

// Pushes the error to the queue
static inline __force_inline void error_push(uint8_t error) {
    uint8_t next = (_err_last + 1) & 15;
    if (next != _err_first) {        
        mia_status_set_flag(MIA_STAT_ERRORS);
        mia_irq_set_flag(IRQ_ERROR);
        _err_buf[_err_last] = error;
        _err_last = next;
    }
}

// Pulls the error from the queue
static inline __force_inline uint8_t error_pull(void) {
    uint8_t head = _err_first;
    
    if (head == _err_last) {
        mia_status_clear_flag(MIA_STAT_ERRORS);
        return 0;
    }
   
    uint8_t val = _err_buf[head];
    _err_first = (head + 1) & 15;

    if (_err_first == _err_last) {
        mia_status_clear_flag(MIA_STAT_ERRORS);
    }

    return val;
}

#endif
