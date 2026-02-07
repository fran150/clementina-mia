#ifndef _MIA_IRQ_H_
#define _MIA_IRQ_H_

#include <stdint.h>

#include "pico/stdlib.h"
#include "mem/regs.h"
#include "hardware/gpio_mapping.h"

#define IRQ_ERROR                (1u << 0)
#define IRQ_IDXA_WRAPPED         (1u << 1)
#define IRQ_IDXB_WRAPPED         (1u << 2)
#define IRQ_COMMAND              (1u << 3)
#define IRQ_TRIGGERED            (1u << 15)

// Configures the IRQ pin as output for the MIA to drive
// and initializes the pin to high.
static inline __force_inline void mia_irq_init(void) {
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);

    mia_regs->irq_mask = 0x0000;
    mia_regs->irq_status = 0x8000;
}

// Evaluates the current value of the irq register and irq masks
// and sets the IRQ_TRIGGERED and IRQ line appropiately
static inline __force_inline void mia_irq_eval() {
    if (mia_regs->irq_status & mia_regs->irq_mask) {
        mia_regs->irq_status |= IRQ_TRIGGERED;
        gpio_put(CPU_IRQB_PIN, false);
    } else {
        mia_regs->irq_status &= ~IRQ_TRIGGERED;
        gpio_put(CPU_IRQB_PIN, true);
    }
}

// Sets the irq flag and triggers IRQ if enabled
static inline __force_inline void mia_irq_set_flag(uint16_t flag) {
    mia_regs->irq_status |= flag;
    mia_irq_eval();
}

#endif