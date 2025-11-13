/**
 * MIA Indexed Memory System
 * 
 * Provides 256 independent memory indexes with automatic stepping,
 * dual-window access, and DMA capabilities for the Clementina 6502 computer.
 */

#ifndef INDEXED_MEMORY_H
#define INDEXED_MEMORY_H

#include <stdint.h>
#include <stdbool.h>

// Index allocation ranges
#define IDX_SYSTEM_ERROR        0
#define IDX_SYSTEM_START        1
#define IDX_SYSTEM_END          15
#define IDX_CHARACTER_START     16
#define IDX_CHARACTER_END       23      // 8 character tables (16-23)
#define IDX_PALETTE_START       32
#define IDX_PALETTE_END         47      // 16 palette banks (32-47)
#define IDX_NAMETABLE_START     48
#define IDX_NAMETABLE_END       51      // 4 nametables (48-51)
#define IDX_PALETTE_TABLE_START 52
#define IDX_PALETTE_TABLE_END   55      // 4 palette tables (52-55)
#define IDX_SPRITE_OAM          56      // Sprite OAM (256 sprites × 4 bytes)
#define IDX_ACTIVE_FRAME        57      // Active frame control (0 or 1 to select buffer set)
#define IDX_VIDEO_RESERVED_START 58
#define IDX_VIDEO_RESERVED_END  63      // Reserved for video expansion
#define IDX_USB_START           64
#define IDX_USB_END             79
#define IDX_SYSCTRL_START       80
#define IDX_SYSCTRL_END         95
#define IDX_RESERVED_START      96
#define IDX_RESERVED_END        127
#define IDX_USER_START          128
#define IDX_USER_END            255

// Configuration field IDs
#define CFG_ADDR_L              0x00
#define CFG_ADDR_M              0x01
#define CFG_ADDR_H              0x02
#define CFG_DEFAULT_L           0x03
#define CFG_DEFAULT_M           0x04
#define CFG_DEFAULT_H           0x05
#define CFG_LIMIT_L             0x06
#define CFG_LIMIT_M             0x07
#define CFG_LIMIT_H             0x08
#define CFG_STEP                0x09
#define CFG_FLAGS               0x0A
#define CFG_COPY_SRC_IDX        0x0B
#define CFG_COPY_DST_IDX        0x0C
#define CFG_COPY_COUNT_L        0x0D
#define CFG_COPY_COUNT_H        0x0E

// Flag bits
#define FLAG_AUTO_STEP          0x01
#define FLAG_DIRECTION          0x02    // 0=forward, 1=backward
#define FLAG_WRAP_ON_LIMIT      0x04    // 0=disabled, 1=wrap to default when reaching limit

// Command codes
#define CMD_NOP                 0x00
#define CMD_RESET_INDEX         0x01
#define CMD_RESET_ALL           0x02
#define CMD_LOAD_DEFAULT        0x03
#define CMD_SET_DEFAULT_TO_ADDR 0x04
#define CMD_CLEAR_IRQ           0x05
#define CMD_COPY_BYTE           0x06
#define CMD_COPY_BLOCK          0x07
#define CMD_SET_COPY_SRC        0x08
#define CMD_SET_COPY_DST        0x09
#define CMD_SET_COPY_COUNT      0x0A
#define CMD_PICO_REINIT         0x10

// Status bits
#define STATUS_BUSY             0x01
#define STATUS_IRQ_PENDING      0x02
#define STATUS_MEMORY_ERROR     0x04
#define STATUS_INDEX_OVERFLOW   0x08
#define STATUS_USB_DATA_READY   0x10
#define STATUS_VIDEO_FRAME_READY 0x20
#define STATUS_DMA_ACTIVE       0x40
#define STATUS_SYSTEM_READY     0x80

// IRQ cause codes (bit masks for 16-bit mask register)
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

// IRQ mask bits (same as cause codes for convenience)
#define IRQ_MASK_MEMORY_ERROR       IRQ_MEMORY_ERROR
#define IRQ_MASK_INDEX_OVERFLOW     IRQ_INDEX_OVERFLOW
#define IRQ_MASK_DMA_COMPLETE       IRQ_DMA_COMPLETE
#define IRQ_MASK_DMA_ERROR          IRQ_DMA_ERROR
#define IRQ_MASK_USB_KEYBOARD       IRQ_USB_KEYBOARD
#define IRQ_MASK_USB_DEVICE_CHANGE  IRQ_USB_DEVICE_CHANGE
#define IRQ_MASK_VIDEO_FRAME        IRQ_VIDEO_FRAME_COMPLETE
#define IRQ_MASK_VIDEO_COLLISION    IRQ_VIDEO_COLLISION

// Memory index structure (16 bytes per index)
typedef struct {
    uint32_t current_addr;      // 24-bit current address (upper 8 bits unused)
    uint32_t default_addr;      // 24-bit default address (upper 8 bits unused)
    uint32_t limit_addr;        // 24-bit limit address for wrap-on-limit (upper 8 bits unused)
    uint8_t step;              // Step size (0-255 bytes)
    uint8_t flags;             // Behavior flags (AUTO_STEP, DIRECTION, WRAP_ON_LIMIT)
    uint16_t reserved;         // Reserved for future use
} index_t;

// Global DMA configuration
typedef struct {
    uint8_t src_idx;
    uint8_t dst_idx;
    uint16_t count;
} dma_config_t;

// System state
typedef struct {
    index_t indexes[256];       // 256 memory indexes
    dma_config_t dma_config;    // DMA operation configuration
    uint8_t status;            // System status register
    uint16_t irq_cause;        // Interrupt cause register (16-bit bit mask)
    uint16_t irq_mask;         // 16-bit interrupt mask register (which IRQs are enabled)
    uint8_t irq_enable;        // Global interrupt enable/disable (1 = enabled, 0 = disabled)
    uint8_t window_a_idx;      // Currently selected index for Window A
    uint8_t window_b_idx;      // Currently selected index for Window B
    uint8_t cfg_field_a;       // Selected configuration field for Window A
    uint8_t cfg_field_b;       // Selected configuration field for Window B
} indexed_memory_state_t;

// Function prototypes
void indexed_memory_init(void);
void indexed_memory_reset_all(void);

// Index management
void indexed_memory_set_index_address(uint8_t idx, uint32_t address);
void indexed_memory_set_index_default(uint8_t idx, uint32_t address);
void indexed_memory_set_index_limit(uint8_t idx, uint32_t address);
void indexed_memory_set_index_step(uint8_t idx, uint8_t step);
void indexed_memory_set_index_flags(uint8_t idx, uint8_t flags);
void indexed_memory_reset_index(uint8_t idx);

// Memory access via indexes
uint8_t indexed_memory_read(uint8_t idx);
void indexed_memory_write(uint8_t idx, uint8_t data);
uint8_t indexed_memory_read_no_step(uint8_t idx);
void indexed_memory_write_no_step(uint8_t idx, uint8_t data);

// Configuration field access
uint8_t indexed_memory_get_config_field(uint8_t idx, uint8_t field);
void indexed_memory_set_config_field(uint8_t idx, uint8_t field, uint8_t value);

// Command execution
void indexed_memory_execute_command(uint8_t cmd);

// DMA operations
// Note: Copy operations use indexes as address pointers only
// Indexes are NOT modified by copy operations (auto-step is ignored)
void indexed_memory_copy_byte(uint8_t src_idx, uint8_t dst_idx);
void indexed_memory_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count);
bool indexed_memory_is_dma_busy(void);

// Status and interrupt management
uint8_t indexed_memory_get_status(void);
uint16_t indexed_memory_get_irq_cause(void);
uint8_t indexed_memory_get_irq_cause_low(void);
uint8_t indexed_memory_get_irq_cause_high(void);
void indexed_memory_write_irq_cause_low(uint8_t clear_bits);
void indexed_memory_write_irq_cause_high(uint8_t clear_bits);
void indexed_memory_clear_irq(void);
void indexed_memory_clear_specific_irq(uint16_t cause);
void indexed_memory_set_irq(uint16_t cause);
uint16_t indexed_memory_get_irq_mask(void);
void indexed_memory_set_irq_mask(uint16_t mask);
uint8_t indexed_memory_get_irq_enable(void);
void indexed_memory_set_irq_enable(uint8_t enable);

// Window management
void indexed_memory_set_window_index(bool window_b, uint8_t idx);
uint8_t indexed_memory_get_window_index(bool window_b);
void indexed_memory_set_config_field_select(bool window_b, uint8_t field);
uint8_t indexed_memory_get_config_field_select(bool window_b);



#endif // INDEXED_MEMORY_H