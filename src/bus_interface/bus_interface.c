/**
 * MIA Bus Interface Module Implementation
 *
 * Implements the 6502 bus interface for the indexed memory system.
 * This version uses a Look-Up Table (LUT) for O(1) register access time.
 *
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * The IO0_CS chip select line indicates we're in indexed interface mode
 */

#include "pico.h"
#include "pico/platform.h"
#include "bus_interface.h"
#include "indexed_memory/indexed_memory.h"
#include "irq/irq.h"
#include <string.h>

// ============================================================================
// Window State Management
// ============================================================================

// Global window state array supporting up to 8 windows (A-H)
window_state_t g_window_state[MAX_WINDOWS];

// ============================================================================
// LUT and Handlers
// ============================================================================

// Place tables in RAM for single-cycle access
static read_handler_t read_lut[256] __attribute__((section(".data")));
static write_handler_t write_lut[256] __attribute__((section(".data")));


// --- NOP Handlers ---
static uint8_t __not_in_flash_func(handle_nop_read)(uint8_t addr) {
    (void)addr;
    return 0x00; // Return 0 for reads from unused or write-only registers
}
static void __not_in_flash_func(handle_nop_write)(uint8_t addr, uint8_t data) {
    (void)addr; (void)data; // Ignore writes to unused or read-only registers
}

// --- Window Read Handlers ---
static uint8_t __not_in_flash_func(handle_data_port_read)(uint8_t addr) {
    uint8_t window_num = (addr >> 4) & 0x07;
    window_state_t *win = get_window_state(window_num);
    return indexed_memory_read(win->active_index);
}

static uint8_t __not_in_flash_func(handle_idx_select_read)(uint8_t addr) {
    uint8_t window_num = (addr >> 4) & 0x07;
    return get_window_state(window_num)->active_index;
}

static uint8_t __not_in_flash_func(handle_cfg_data_read)(uint8_t addr) {
    uint8_t window_num = (addr >> 4) & 0x07;
    window_state_t *win = get_window_state(window_num);
    return indexed_memory_get_config_field(win->active_index, win->config_field_select);
}

static uint8_t __not_in_flash_func(handle_cfg_field_select_read)(uint8_t addr) {
    uint8_t window_num = (addr >> 4) & 0x07;
    return get_window_state(window_num)->config_field_select;
}


// --- Window Write Handlers ---
static void __not_in_flash_func(handle_data_port_write)(uint8_t addr, uint8_t data) {
    uint8_t window_num = (addr >> 4) & 0x07;
    window_state_t *win = get_window_state(window_num);
    indexed_memory_write(win->active_index, data);
}

static void __not_in_flash_func(handle_idx_select_write)(uint8_t addr, uint8_t data) {
    uint8_t window_num = (addr >> 4) & 0x07;
    get_window_state(window_num)->active_index = data;
}

static void __not_in_flash_func(handle_cfg_data_write)(uint8_t addr, uint8_t data) {
    uint8_t window_num = (addr >> 4) & 0x07;
    window_state_t *win = get_window_state(window_num);
    indexed_memory_set_config_field(win->active_index, win->config_field_select, data);
}

static void __not_in_flash_func(handle_cfg_field_select_write)(uint8_t addr, uint8_t data) {
    uint8_t window_num = (addr >> 4) & 0x07;
    get_window_state(window_num)->config_field_select = data;
}

static void __not_in_flash_func(handle_window_command_write)(uint8_t addr, uint8_t data) {
    uint8_t window_num = (addr >> 4) & 0x07;
    uint8_t idx = get_window_state(window_num)->active_index;
    indexed_memory_execute_window_command(idx, data);
}


// --- Shared Read Handlers ---
static uint8_t __not_in_flash_func(handle_device_status_read)(uint8_t addr) {
    (void)addr;
    return indexed_memory_get_status() | (irq_is_pending() ? STATUS_IRQ_PENDING : 0);
}
static uint8_t __not_in_flash_func(handle_irq_cause_low_read)(uint8_t addr) { (void)addr; return irq_get_cause_low(); }
static uint8_t __not_in_flash_func(handle_irq_cause_high_read)(uint8_t addr) { (void)addr; return irq_get_cause_high(); }
static uint8_t __not_in_flash_func(handle_irq_mask_low_read)(uint8_t addr) { (void)addr; return irq_get_mask_low(); }
static uint8_t __not_in_flash_func(handle_irq_mask_high_read)(uint8_t addr) { (void)addr; return irq_get_mask_high(); }
static uint8_t __not_in_flash_func(handle_irq_enable_read)(uint8_t addr) { (void)addr; return irq_get_enable(); }


// --- Shared Write Handlers ---
static void __not_in_flash_func(handle_irq_cause_low_write)(uint8_t addr, uint8_t data) { (void)addr; irq_write_cause_low(data); }
static void __not_in_flash_func(handle_irq_cause_high_write)(uint8_t addr, uint8_t data) { (void)addr; irq_write_cause_high(data); }
static void __not_in_flash_func(handle_irq_mask_low_write)(uint8_t addr, uint8_t data) { (void)addr; irq_set_mask_low(data); }
static void __not_in_flash_func(handle_irq_mask_high_write)(uint8_t addr, uint8_t data) { (void)addr; irq_set_mask_high(data); }
static void __not_in_flash_func(handle_irq_enable_write)(uint8_t addr, uint8_t data) { (void)addr; irq_set_enable(data); }
static void __not_in_flash_func(handle_shared_command_write)(uint8_t addr, uint8_t data) { (void)addr; indexed_memory_execute_shared_command(data); }


// ============================================================================
// Module Initialization
// ============================================================================

void bus_interface_init(void) {
    // Initialize all window states to default values
    memset(g_window_state, 0, sizeof(g_window_state));

    // Populate the LUTs
    for (int i = 0; i < 256; i++) {
        bool is_shared = (i >= 0x80);
        uint8_t reg_offset = i & 0x0F;

        // Default to NOP handlers
        read_lut[i] = handle_nop_read;
        write_lut[i] = handle_nop_write;

        if (is_shared) {
            switch (i) {
                case REG_DEVICE_STATUS:  read_lut[i] = handle_device_status_read; break;
                case REG_IRQ_CAUSE_LOW:  read_lut[i] = handle_irq_cause_low_read;  write_lut[i] = handle_irq_cause_low_write;  break;
                case REG_IRQ_CAUSE_HIGH: read_lut[i] = handle_irq_cause_high_read; write_lut[i] = handle_irq_cause_high_write; break;
                case REG_IRQ_MASK_LOW:   read_lut[i] = handle_irq_mask_low_read;   write_lut[i] = handle_irq_mask_low_write;   break;
                case REG_IRQ_MASK_HIGH:  read_lut[i] = handle_irq_mask_high_read;  write_lut[i] = handle_irq_mask_high_write;  break;
                case REG_IRQ_ENABLE:     read_lut[i] = handle_irq_enable_read;     write_lut[i] = handle_irq_enable_write;     break;
                case REG_SHARED_COMMAND: write_lut[i] = handle_shared_command_write; break;
            }
        } else { // Windowed registers
            switch (reg_offset) {
                case REG_OFFSET_IDX_SELECT:
                    read_lut[i]  = handle_idx_select_read;
                    write_lut[i] = handle_idx_select_write;
                    break;
                case REG_OFFSET_DATA_PORT:
                    read_lut[i]  = handle_data_port_read;
                    write_lut[i] = handle_data_port_write;
                    break;
                case REG_OFFSET_CFG_FIELD_SELECT:
                    read_lut[i]  = handle_cfg_field_select_read;
                    write_lut[i] = handle_cfg_field_select_write;
                    break;
                case REG_OFFSET_CFG_DATA:
                    read_lut[i]  = handle_cfg_data_read;
                    write_lut[i] = handle_cfg_data_write;
                    break;
                case REG_OFFSET_COMMAND:
                    write_lut[i] = handle_window_command_write;
                    break;
            }
        }
    }
}


// ============================================================================
// Main Bus Interface Handlers (Hot Path)
// ============================================================================

uint8_t __not_in_flash_func(bus_interface_read)(uint8_t local_addr) {
    return read_lut[local_addr](local_addr);
}

void __not_in_flash_func(bus_interface_write)(uint8_t local_addr, uint8_t data) {
    write_lut[local_addr](local_addr, data);
}