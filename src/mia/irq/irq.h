#ifndef _MIA_IRQ_H_
#define _MIA_IRQ_H_

#include <stdint.h>
#include <stdatomic.h>

#include "pico/stdlib.h"
#include "mem/regs.h"
#include "hardware/gpio_mapping.h"

#define IRQ_ERROR                (1u << 0)
#define IRQ_IDXA_WRAPPED         (1u << 1)
#define IRQ_IDXB_WRAPPED         (1u << 2)
#define IRQ_COMMAND              (1u << 3)
#define IRQ_SPEED_CHANGED        (1u << 4)
#define IRQ_VIDEO_FRAME_REQUEST  (1u << 5)
#define IRQ_VIDEO_FRAME_SENT     (1u << 6)
#define IRQ_VIDEO_FRAME_ACKED    (1u << 7)
#define IRQ_TRIGGERED            (1u << 15)   // summary bit, maintained by core 1

// IRQ ownership model
// -------------------
// Core 1 (the action loop) is the SOLE writer of mia_regs->irq_status and the
// SOLE driver of the IRQ pin. Any core or interrupt context that wants to raise
// an IRQ source ORs the bit into mia_irq_set_requests (atomic, so multiple
// producers across both cores are safe); core 1 folds those requests into
// irq_status and updates the line on its next pass. The 6502 clears all source
// bits by reading $FFF0 (read-to-clear), which core 1 handles in the act_loop.
// This keeps irq_status free of cross-core read-modify-write races and free of
// the write-DMA that commits the register block, without taking any lock on the
// hot path.

// Multi-producer set-request accumulator, drained by core 1. Defined in mia.c.
extern atomic_ushort mia_irq_set_requests;

// Configures the IRQ pin as output (idle high) and clears all IRQ state.
static inline __force_inline void mia_irq_init(void) {
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);

    mia_regs->irq_mask = 0x0000;
    mia_regs->irq_status = 0x0000;
    atomic_store(&mia_irq_set_requests, 0);
}

// Core 1 only. Recomputes the IRQ_TRIGGERED summary bit and the active-low pin
// from the given status/mask and writes the result back into irq_status. The
// summary bit itself is excluded from the "is anything pending" test so it never
// feeds back on itself.
static inline __force_inline void mia_irq_apply(uint16_t status, uint16_t mask) {
    bool triggered = (status & mask & (uint16_t)~IRQ_TRIGGERED) != 0;
    bool was_triggered = (mia_regs->irq_status & IRQ_TRIGGERED) != 0;

    if (triggered) {
        status |= IRQ_TRIGGERED;
    } else {
        status &= (uint16_t)~IRQ_TRIGGERED;
    }

    // Only write to GPIO when the pin state actually changes.
    if (triggered != was_triggered) {
        gpio_put(CPU_IRQB_PIN, !triggered);  // active low: assert=false, deassert=true
    }

    mia_regs->irq_status = status;
}

// Core 1 only. Folds any pending set-requests from other contexts into
// irq_status and updates the line. Cheap when there is nothing pending.
static inline __force_inline void mia_irq_drain_requests(void) {
    if (mia_irq_set_requests) {
        uint16_t req = atomic_exchange(&mia_irq_set_requests, 0);
        mia_irq_apply(mia_regs->irq_status | req, mia_regs->irq_mask);
    }
}

// Core 1 only. Handles a 6502 write to IRQ_MASK ($FFEE low, $FFEF high).
// Overlays the freshly written byte onto the live 16-bit mask — the write-DMA
// that commits irq_mask may not have landed this same-cycle byte yet, so the
// caller passes address and data directly from the action FIFO.
static inline __force_inline void mia_irq_write_mask(uint32_t address, uint8_t data) {
    uint16_t mask = mia_regs->irq_mask;
    if (address & 1) {
        mask = (mask & 0x00FF) | ((uint16_t)data << 8);
    } else {
        mask = (mask & 0xFF00) | data;
    }
    mia_irq_apply(mia_regs->irq_status, mask);
}

// Any core or interrupt context. Requests that the given IRQ source bits be set;
// core 1 folds them into irq_status on its next pass. The status bit latches
// regardless of the mask; the physical line only asserts for sources that are
// enabled in IRQ_MASK. The 6502 clears all status bits by reading $FFF0.
static inline __force_inline void mia_irq_set_flag(uint16_t flag) {
    atomic_fetch_or(&mia_irq_set_requests, flag);
}

#endif
