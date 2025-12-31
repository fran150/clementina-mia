/**
 * ROM Emulation Implementation
 * Boot-time ROM functionality using C code at 100 kHz
 */

#include "rom_emulator.h"
#include "kernel_data.h"
#include "debug/debug_helper.h"
#include "hardware/gpio_mapping.h"
#include "system/phi2_clock.h"
#include "system/reset_control.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include <stdio.h>

// TODO: Temp definition
#define CLOCK_PHASE_BOOT   100000
#define CLOCK_PHASE_NORMAL 100000

static rom_state_t current_state = ROM_STATE_INACTIVE;
static uint32_t kernel_data_pointer = -1;
static absolute_time_t reset_start_time;
static uint32_t reset_cycle_count = 0;

static bool kernel_pointer_block_move = false;

// Forward declarations for internal functions
static bool rom_emulator_handle_read(uint8_t address, uint8_t *data);

// 6502 Boot Loader Assembly Code
// This code runs on the 6502 CPU and copies the kernel from MIA to RAM
// Memory addresses shown are the 6502 CPU addresses ($E000-$E0FF)
static const uint8_t bootloader_code[] = {
    // === Kernel Loader Entry Point ($E000) ===
    0x78,                    // $E000: SEI - Disable interrupts during loading
    0xD8,                    // $E001: CLD - Clear decimal mode
    
    // Set up destination pointer for kernel loading at $4000
    0xA9, 0x00,              // $E002-$E003: LDA #$00 - Low byte of $4000
    0x85, 0x00,              // $E004-$E005: STA $00 - Store in zero page pointer (low byte)
    0xA9, 0x40,              // $E006-$E007: LDA #$40 - High byte of $4000
    0x85, 0x01,              // $E008-$E009: STA $01 - Store in zero page pointer (high byte)
    
    // Initialize Y register as offset
    0xA0, 0x00,              // $E00A-$E00B: LDY #$00
    
    // === Main Kernel Loading Loop ===
    // LOAD_LOOP: ($E00C)
    0xAD, 0x80, 0xE0,        // $E00C-$E00E: LDA $E080 - Read status address (mapped to MIA status register)
    0xF0, 0x0D,              // $E00F-$E010: BEQ LOAD_COMPLETE - If 0, loading is complete (branch to $E01D)
    
    // Read next kernel byte
    0xAD, 0x81, 0xE0,        // $E011-$E013: LDA $E081 - Read data address (mapped to MIA data register)
    0x91, 0x00,              // $E014-$E015: STA ($00),Y - Store byte at destination address
    
    // Advance destination pointer
    0xC8,                    // $E016: INY - Increment offset
    0xD0, 0xF3,              // $E017-$E018: BNE LOAD_LOOP - If Y didn't wrap, continue (branch to $E00C)
    
    // Handle page boundary crossing
    0xE6, 0x01,              // $E019-$E01A: INC $01 - Increment high byte of destination
    0x4C, 0x0C, 0xE0,        // $E01B-$E01D: JMP LOAD_LOOP - Continue loading (absolute jump to $E00C)
    
    // === Loading Complete ===
    // LOAD_COMPLETE: ($E01E)
    0x4C, 0x00, 0x40,        // $E01E-$E020: JMP $4000 - Jump directly to kernel entry point
};

// Kernel data is now loaded from kernel.bin at build time
// See kernel_data.h for external declarations

void rom_emulator_init(void) {
    current_state = ROM_STATE_INACTIVE;
    kernel_data_pointer = -1;
    reset_cycle_count = 0;
    
    log_print(LOG_INFO, "ROM Emulator initialized - Boot loader: %zu bytes, Kernel: %zu bytes\n", 
           sizeof(bootloader_code), kernel_data_size);
}

void rom_emulator_start_boot_sequence(void) {
    if (current_state == ROM_STATE_INACTIVE) {
        log_print(LOG_INFO, "Starting boot sequence...\n");
        
        // Assert reset for minimum 5 cycles at 100 kHz
        reset_control_assert_reset();
        current_state = ROM_STATE_RESET_SEQUENCE;
        reset_start_time = get_absolute_time();
        reset_cycle_count = 0;
                
        log_print(LOG_INFO, "Reset asserted, waiting for 5+ clock cycles...\n");
    }
}

void rom_emulator_process(void) {
    bool we, oe;
    
    // Handle reset sequence timing
    if (current_state == ROM_STATE_RESET_SEQUENCE) {
        // Calculate elapsed time and cycles
        int64_t elapsed_us = absolute_time_diff_us(reset_start_time, get_absolute_time());
        uint32_t elapsed_cycles = (elapsed_us * CLOCK_PHASE_BOOT) / 1000000;
        
        // Release reset after minimum 5 cycles (50us at 100kHz)
        if (elapsed_cycles >= 5 && elapsed_us >= 50) {
            // Bank MIA into high memory space
            gpio_put(GPIO_PICOHIRAM, 0);  // Active low

            reset_control_release_reset();
            current_state = ROM_STATE_BOOT_ACTIVE;
                        
            log_print(LOG_INFO, "Reset released after %lld us (%lu cycles), MIA banked into high memory\n", 
                   elapsed_us, elapsed_cycles);
        }
        return;
    }
    
    // Read control signals
    bool hiram_cs, io0_cs;
    gpio_read_control_signals(&we, &oe, &hiram_cs, &io0_cs);
    
    // Only process if ROM chip select is active and we're in boot phase
    if (hiram_cs && (current_state == ROM_STATE_BOOT_ACTIVE || current_state == ROM_STATE_KERNEL_LOADING)) {
        uint8_t address = gpio_read_address_bus();

        if (oe && !we) {  // Read operation
            uint8_t data;
            if (rom_emulator_handle_read(address, &data)) {
                gpio_set_data_bus_direction(true);  // Set as output
                gpio_write_data_bus(data);

                uint16_t offset = address - BOOTLOADER_START;
                if (offset == (sizeof(bootloader_code)) - 1) {
                    sleep_ms(1000);
                    current_state = ROM_STATE_COMPLETE;
                    log_print(LOG_INFO, "All kernel data transferred\n");
                }
            }
        }
    } else if ((current_state == ROM_STATE_BOOT_ACTIVE || current_state == ROM_STATE_KERNEL_LOADING) && !hiram_cs) {
        // ROM no longer selected, set data bus back to input
        gpio_set_data_bus_direction(false);
    }
    
    // Check for completion and phase transition
    if (current_state == ROM_STATE_COMPLETE) {
        log_print(LOG_INFO, "Kernel loading complete, transitioning to normal operation\n");
        
        // Transition to normal operation
        //phi2_clock_init(CLOCK_PHASE_NORMAL);
        gpio_put(GPIO_PICOHIRAM, 1);  // Bank out of high memory
                
        current_state = ROM_STATE_INACTIVE;
        
        log_print(LOG_INFO, "MIA banked out, clock increased to 1 MHz, bus interface activated\n");
    }
}

static bool rom_emulator_handle_read(uint8_t address, uint8_t *data) {
    if (!data) return false;
    
    // Handle reset vector ($FFFC-$FFFD maps to $FC-$FD in MIA space)
    if (address == ROM_RESET_VECTOR) {
        // Return low byte of boot loader start address ($E000)
        *data = 0x00;  // Low byte of $E000
        return true;
    } else if (address == ROM_RESET_VECTOR + 1) {
        // Return high byte of boot loader start address ($E000)
        *data = 0xE0;  // High byte of $E000
        return true;
    }
    
    // Handle boot loader code ($E000-$E07F maps to $000-$07F in MIA space)
    else if (address < KERNEL_STATUS_ADDR) {
        uint16_t offset = address - BOOTLOADER_START;
        if (offset < sizeof(bootloader_code)) {
            *data = bootloader_code[offset];
        } else {
            *data = 0xEA;  // NOP padding
        }
        return true;
    }
    
    // Handle kernel status register ($E080 maps to $080 in MIA space)
    else if (address == KERNEL_STATUS_ADDR) {
        if (current_state != ROM_STATE_KERNEL_LOADING) {
            current_state = ROM_STATE_KERNEL_LOADING;
            log_print(LOG_INFO, "Kernel loading started by 6502 CPU\n");
        }

        if (!kernel_pointer_block_move) {
            kernel_pointer_block_move = true;
            kernel_data_pointer++;
        }
        
        // Return 1 if more data available, 0 if complete
        *data = (kernel_data_pointer < kernel_data_size) ? 0x01 : 0x00;
        return true;
    }
    
    // Handle kernel data register ($E081 maps to $081 in MIA space)
    else if (address == KERNEL_DATA_ADDR) {
        if (kernel_data_pointer < kernel_data_size) {
            *data = kernel_data[kernel_data_pointer];
            kernel_pointer_block_move = false;

            // Log progress periodically
            if (kernel_data_pointer % 64 == 0 || kernel_data_pointer >= kernel_data_size) {
                log_print(LOG_INFO, "Kernel transfer progress: %lu/%zu bytes\n", 
                       kernel_data_pointer, kernel_data_size);
            }            
        } else {
            *data = 0x00;  // No more data
        }
        return true;
    }
    
    // Default case - return NOP for unmapped addresses
    *data = 0xEA;  // NOP
    return true;
}

bool rom_emulator_is_active(void) {
    return current_state != ROM_STATE_INACTIVE;
}