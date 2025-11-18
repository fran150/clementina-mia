/**
 * MIA IRQ System Implementation
 * 
 * Centralized interrupt management for the MIA system.
 */

#include "irq.h"
#include "hardware/gpio_mapping.h"

// Always include GPIO for IRQ line control (mocked in tests)
#include "hardware/gpio.h"

// IRQ system state
typedef struct {
    uint16_t irq_cause;        // Interrupt cause register (16-bit bit mask)
    uint16_t irq_mask;         // 16-bit interrupt mask register (which IRQs are enabled)
    uint8_t irq_enable;        // Global interrupt enable/disable (1 = enabled, 0 = disabled)
} irq_state_t;

// Private state
static irq_state_t g_irq_state;

/**
 * Assert IRQ line to 6502 (active low)
 */
static void assert_irq_line(void) {
    gpio_put(GPIO_IRQ_OUT, 0);  // Active low
}

/**
 * Deassert IRQ line to 6502 (inactive high)
 */
static void deassert_irq_line(void) {
    gpio_put(GPIO_IRQ_OUT, 1);  // Inactive high
}

/**
 * Initialize IRQ system
 */
void irq_init(void) {
    g_irq_state.irq_cause = IRQ_NO_IRQ;
    g_irq_state.irq_mask = 0xFFFF; // All interrupts enabled by default (16-bit)
    g_irq_state.irq_enable = 0x01; // Global interrupts enabled by default
    
    // Deassert IRQ line initially
    deassert_irq_line();
}

/**
 * Set interrupt cause and assert IRQ line if enabled
 */
void irq_set_bits(uint16_t cause) {
    // Set the interrupt bit(s) in the pending register (OR to accumulate)
    g_irq_state.irq_cause |= cause;
    
    // Check if any enabled interrupts are pending and global enable is on
    if (g_irq_state.irq_enable && ((g_irq_state.irq_cause & g_irq_state.irq_mask) != 0)) {
        assert_irq_line();
    }
}

/**
 * Clear interrupt cause and deassert IRQ line if no more enabled interrupts
 */
void irq_clear_bits(uint16_t cause) {
    // Clear the interrupt bit(s) from the pending register
    g_irq_state.irq_cause &= ~cause;
    
    // If no enabled interrupts are pending, deassert IRQ line
    if ((g_irq_state.irq_cause & g_irq_state.irq_mask) == 0) {
        deassert_irq_line();
    }
}

/**
 * Clear all interrupts and deassert IRQ line
 */
void irq_clear_all(void) {
    g_irq_state.irq_cause = IRQ_NO_IRQ;
    deassert_irq_line();
}

/**
 * Get full 16-bit IRQ cause
 */
uint16_t irq_get_cause(void) {
    return g_irq_state.irq_cause;
}

/**
 * Get IRQ cause low byte (bits 0-7)
 */
uint8_t irq_get_cause_low(void) {
    return g_irq_state.irq_cause & 0xFF;
}

/**
 * Get IRQ cause high byte (bits 8-15)
 */
uint8_t irq_get_cause_high(void) {
    return (g_irq_state.irq_cause >> 8) & 0xFF;
}

/**
 * Write-1-to-clear IRQ cause low byte
 */
void irq_write_cause_low(uint8_t clear_bits) {
    // Clear specified bits in low byte
    g_irq_state.irq_cause &= ~((uint16_t)clear_bits);
    
    // If no enabled interrupts are pending, deassert IRQ line
    if ((g_irq_state.irq_cause & g_irq_state.irq_mask) == 0) {
        deassert_irq_line();
    }
}

/**
 * Write-1-to-clear IRQ cause high byte
 */
void irq_write_cause_high(uint8_t clear_bits) {
    // Clear specified bits in high byte
    g_irq_state.irq_cause &= ~((uint16_t)clear_bits << 8);
    
    // If no enabled interrupts are pending, deassert IRQ line
    if ((g_irq_state.irq_cause & g_irq_state.irq_mask) == 0) {
        deassert_irq_line();
    }
}

/**
 * Get IRQ mask
 */
uint16_t irq_get_mask(void) {
    return g_irq_state.irq_mask;
}

/**
 * Set IRQ mask and update IRQ line state
 */
void irq_set_mask(uint16_t mask) {
    g_irq_state.irq_mask = mask;
    
    // Re-evaluate IRQ line state based on new mask
    if ((g_irq_state.irq_cause & g_irq_state.irq_mask) == 0) {
        deassert_irq_line();
    } else if (g_irq_state.irq_enable) {
        assert_irq_line();
    }
}

/**
 * Get global IRQ enable state
 */
uint8_t irq_get_enable(void) {
    return g_irq_state.irq_enable;
}

/**
 * Set global IRQ enable and update IRQ line state
 */
void irq_set_enable(uint8_t enable) {
    g_irq_state.irq_enable = enable ? 0x01 : 0x00;
    
    // Re-evaluate IRQ line state
    if (g_irq_state.irq_enable) {
        if ((g_irq_state.irq_cause & g_irq_state.irq_mask) != 0) {
            assert_irq_line();
        }
    } else {
        deassert_irq_line();
    }
}

/**
 * Check if any IRQ is pending
 */
bool irq_is_pending(void) {
    return g_irq_state.irq_enable && ((g_irq_state.irq_cause & g_irq_state.irq_mask) != 0);
}
