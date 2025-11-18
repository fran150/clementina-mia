/**
 * MIA IRQ System
 * 
 * Centralized interrupt management for the MIA system.
 * Handles interrupt causes, masking, and GPIO line control.
 */

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

// IRQ cause codes and mask bits (same values used for both purposes)
// Low byte (bits 0-7): System and I/O interrupts
#define IRQ_NO_IRQ              0x0000  // Special value: no interrupt
#define IRQ_MEMORY_ERROR        0x0001  // Bit 0 (low byte)
#define IRQ_INDEX_OVERFLOW      0x0002  // Bit 1 (low byte)
#define IRQ_DMA_COMPLETE        0x0004  // Bit 2 (low byte)
#define IRQ_DMA_ERROR           0x0008  // Bit 3 (low byte)
#define IRQ_USB_KEYBOARD        0x0010  // Bit 4 (low byte)
#define IRQ_USB_DEVICE_CHANGE   0x0020  // Bit 5 (low byte)
#define IRQ_RESERVED_6          0x0040  // Bit 6 (low byte) - reserved
#define IRQ_RESERVED_7          0x0080  // Bit 7 (low byte) - reserved

// High byte (bits 8-15): Video interrupts
#define IRQ_VIDEO_FRAME_COMPLETE 0x0100 // Bit 8 (high byte, bit 0)
#define IRQ_VIDEO_COLLISION     0x0200  // Bit 9 (high byte, bit 1)
#define IRQ_RESERVED_10         0x0400  // Bit 10 (high byte, bit 2) - reserved
#define IRQ_RESERVED_11         0x0800  // Bit 11 (high byte, bit 3) - reserved
#define IRQ_RESERVED_12         0x1000  // Bit 12 (high byte, bit 4) - reserved
#define IRQ_RESERVED_13         0x2000  // Bit 13 (high byte, bit 5) - reserved
#define IRQ_RESERVED_14         0x4000  // Bit 14 (high byte, bit 6) - reserved
#define IRQ_RESERVED_15         0x8000  // Bit 15 (high byte, bit 7) - reserved

// Status register bits
#define STATUS_IRQ_PENDING      0x02

// Public API
void irq_init(void);
void irq_set_bits(uint16_t cause);
void irq_clear_bits(uint16_t cause);
void irq_clear_all(void);
uint16_t irq_get_cause(void);
uint8_t irq_get_cause_low(void);
uint8_t irq_get_cause_high(void);
void irq_write_cause_low(uint8_t clear_bits);
void irq_write_cause_high(uint8_t clear_bits);
uint16_t irq_get_mask(void);
void irq_set_mask(uint16_t mask);
uint8_t irq_get_enable(void);
void irq_set_enable(uint8_t enable);
bool irq_is_pending(void);

#endif // IRQ_H
