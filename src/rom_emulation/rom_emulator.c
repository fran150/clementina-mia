/**
 * ROM Emulation Implementation
 * Boot-time ROM functionality using C code at 100 kHz
 */

#include "rom_emulator.h"
#include "hardware/gpio_mapping.h"
#include "system/clock_control.h"
#include "system/reset_control.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include <stdio.h>

static rom_state_t current_state = ROM_STATE_INACTIVE;
static uint32_t kernel_data_pointer = 0;
static absolute_time_t reset_start_time;
static uint32_t reset_cycle_count = 0;

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
    0xAD, 0x00, 0xE1,        // $E00C-$E00E: LDA $E100 - Read status address (mapped to MIA status register)
    0xF0, 0x0C,              // $E00F-$E010: BEQ LOAD_COMPLETE - If 0, loading is complete (branch to $E01D)
    
    // Read next kernel byte
    0xAD, 0x01, 0xE1,        // $E011-$E013: LDA $E101 - Read data address (mapped to MIA data register)
    0x91, 0x00,              // $E014-$E015: STA ($00),Y - Store byte at destination address
    
    // Advance destination pointer
    0xC8,                    // $E016: INY - Increment offset
    0xD0, 0xF4,              // $E017-$E018: BNE LOAD_LOOP - If Y didn't wrap, continue (branch to $E00C)
    
    // Handle page boundary crossing
    0xE6, 0x01,              // $E019-$E01A: INC $01 - Increment high byte of destination
    0x4C, 0x0C, 0xE0,        // $E01B-$E01D: JMP LOAD_LOOP - Continue loading (absolute jump to $E00C)
    
    // === Loading Complete ===
    // LOAD_COMPLETE: ($E01E)
    0x4C, 0x00, 0x40,        // $E01E-$E020: JMP $4000 - Jump directly to kernel entry point
    
    // Padding to ensure we don't exceed our space
    0xEA, 0xEA, 0xEA, 0xEA,  // $E021-$E024: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E025-$E028: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E029-$E02C: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E02D-$E030: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E031-$E034: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E035-$E038: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E039-$E03C: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E03D-$E040: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E041-$E044: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E045-$E048: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E049-$E04C: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E04D-$E050: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E051-$E054: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E055-$E058: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA,  // $E059-$E05C: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA   // $E05D-$E060: NOP padding
};

// Complete Clementina Kernel Binary
// This is a minimal 6502 kernel that demonstrates system initialization
// Memory addresses shown are the 6502 CPU addresses where this code will be loaded ($4000+)
static const uint8_t kernel_data[] = {
    // === Kernel Entry Point ($4000) ===
    0x78,                    // $4000: SEI - Disable interrupts
    0xD8,                    // $4001: CLD - Clear decimal mode
    
    // Initialize stack pointer
    0xA2, 0xFF,              // $4002-$4003: LDX #$FF
    0x9A,                    // $4004: TXS - Set stack pointer to $01FF
    
    // Bank out MIA by asserting PICOHIRAM and increase clock speed
    // This would typically be done through a memory-mapped register
    // For now, we'll use a placeholder address
    0xA9, 0x01,              // $4005-$4006: LDA #$01
    0x8D, 0x00, 0xC1,        // $4007-$4009: STA $C100 - Signal MIA to bank out and increase speed
    
    // Initialize system variables
    0xA9, 0x00,              // $400A-$400B: LDA #$00
    0x8D, 0x00, 0x02,        // $400C-$400E: STA $0200 - Clear system status
    0x8D, 0x01, 0x02,        // $400F-$4011: STA $0201 - Clear error flags
    
    // Set up interrupt vectors (example)
    0xA9, 0x00,              // $4012-$4013: LDA #$00 - Low byte of IRQ handler
    0x8D, 0xFE, 0xFF,        // $4014-$4016: STA $FFFE
    0xA9, 0x50,              // $4017-$4018: LDA #$50 - High byte of IRQ handler ($5000)
    0x8D, 0xFF, 0xFF,        // $4019-$401B: STA $FFFF
    
    // Initialize video system (placeholder)
    0xA9, 0x80,              // $401C-$401D: LDA #$80 - Video init command
    0x8D, 0x00, 0xD0,        // $401E-$4020: STA $D000 - Video control register
    
    // Clear screen memory (example: clear first 1KB of video memory)
    0xA9, 0x20,              // $4021-$4022: LDA #$20 - Space character
    0xA2, 0x00,              // $4023-$4024: LDX #$00
    0xA0, 0x00,              // $4025-$4026: LDY #$00
    
    // CLEAR_LOOP: ($4027)
    0x99, 0x00, 0x30,        // $4027-$4029: STA $3000,Y - Store space character (example screen memory)
    0xC8,                    // $402A: INY
    0xD0, 0xFA,              // $402B-$402C: BNE CLEAR_LOOP - Continue if Y != 0 (branch to $4027)
    0xEE, 0x29, 0x40,        // $402D-$402F: INC $4029 - Increment high byte of store instruction
    0xE8,                    // $4030: INX
    0xE0, 0x04,              // $4031-$4032: CPX #$04 - Compare with 4 (4 pages = 1KB)
    0xD0, 0xF2,              // $4033-$4034: BNE CLEAR_LOOP - Continue if not done (branch to $4027)
    
    // Enable interrupts and enter main loop
    0x58,                    // $4035: CLI - Enable interrupts
    
    // === Main Kernel Loop ===
    // MAIN_LOOP: ($4036)
    0xAD, 0x00, 0xC0,        // $4036-$4038: LDA $C000 - Check keyboard input
    0xF0, 0x06,              // $4039-$403A: BEQ NO_KEY - Branch if no key pressed (to $4041)
    
    // Process keyboard input
    0x8D, 0x00, 0x30,        // $403B-$403D: STA $3000 - Display character on screen
    0xEE, 0x01, 0x30,        // $403E-$4040: INC $3001 - Move cursor
    
    // NO_KEY: ($4041)
    0xAD, 0x01, 0xD0,        // $4041-$4043: LDA $D001 - Check video status
    0x29, 0x80,              // $4044-$4045: AND #$80 - Check VBlank flag
    0xF0, 0x02,              // $4046-$4047: BEQ SKIP_VBLANK (to $404A)
    
    // Handle VBlank processing
    0xEE, 0x02, 0x02,        // $4048-$404A: INC $0202 - Increment frame counter
    
    // SKIP_VBLANK: ($404B)
    0x4C, 0x36, 0x40,        // $404B-$404D: JMP MAIN_LOOP - Jump back to main loop ($4036)
    
    // === IRQ Handler (would be copied to $5000) ===
    // This code would be copied to $5000 in actual memory by the kernel
    0x48,                    // $404E: PHA - Save accumulator
    0x8A,                    // $404F: TXA
    0x48,                    // $4050: PHA - Save X register
    0x98,                    // $4051: TYA
    0x48,                    // $4052: PHA - Save Y register
    
    // Handle interrupt (placeholder)
    0xAD, 0x00, 0xD5,        // $4053-$4055: LDA $D500 - Read interrupt source
    0x29, 0x01,              // $4056-$4057: AND #$01 - Check timer interrupt
    0xF0, 0x04,              // $4058-$4059: BEQ CHECK_OTHER (to $405E)
    
    // Handle timer interrupt
    0xEE, 0x03, 0x02,        // $405A-$405C: INC $0203 - Increment timer counter
    
    // CHECK_OTHER: ($405E)
    // Additional interrupt handling would go here
    
    // Restore registers and return
    0x68,                    // $405E: PLA - Restore Y register
    0xA8,                    // $405F: TAY
    0x68,                    // $4060: PLA - Restore X register
    0xAA,                    // $4061: TAX
    0x68,                    // $4062: PLA - Restore accumulator
    0x40,                    // $4063: RTI - Return from interrupt
    
    // Padding and additional kernel code space
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $4064-$406B: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $406C-$4073: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $4074-$407B: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $407C-$4083: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $4084-$408B: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $408C-$4093: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,  // $4094-$409B: NOP padding
    0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA   // $409C-$40A3: NOP padding
};

void rom_emulator_init(void) {
    current_state = ROM_STATE_INACTIVE;
    kernel_data_pointer = 0;
    reset_cycle_count = 0;
    
    printf("ROM Emulator initialized - Boot loader: %zu bytes, Kernel: %zu bytes\n", 
           sizeof(bootloader_code), sizeof(kernel_data));
}

void rom_emulator_start_boot_sequence(void) {
    if (current_state == ROM_STATE_INACTIVE) {
        printf("Starting boot sequence...\n");
        
        // Assert reset for minimum 5 cycles at 100 kHz
        reset_control_assert_reset();
        current_state = ROM_STATE_RESET_SEQUENCE;
        reset_start_time = get_absolute_time();
        reset_cycle_count = 0;
        
        // Ensure we're in boot phase with 100 kHz clock
        clock_control_set_phase(CLOCK_PHASE_BOOT);
        
        printf("Reset asserted, waiting for 5+ clock cycles...\n");
    }
}

void rom_emulator_process(void) {
    bool we, oe, rom_cs, video_cs, gen_cs;
    
    // Handle reset sequence timing
    if (current_state == ROM_STATE_RESET_SEQUENCE) {
        // Calculate elapsed time and cycles
        int64_t elapsed_us = absolute_time_diff_us(reset_start_time, get_absolute_time());
        uint32_t elapsed_cycles = (elapsed_us * CLOCK_FREQ_BOOT) / 1000000;
        
        // Release reset after minimum 5 cycles (50us at 100kHz)
        if (elapsed_cycles >= 5 && elapsed_us >= 50) {
            // Bank MIA into high memory space
            gpio_put(GPIO_PICOHIRAM, 0);  // Active low

            reset_control_release_reset();
            current_state = ROM_STATE_BOOT_ACTIVE;
                        
            printf("Reset released after %lld us (%lu cycles), MIA banked into high memory\n", 
                   elapsed_us, elapsed_cycles);
        }
        return;
    }
    
    // Read control signals
    gpio_read_control_signals(&we, &oe, &rom_cs, &video_cs, &gen_cs);
    
    // Only process if ROM chip select is active and we're in boot phase
    if (rom_cs && (current_state == ROM_STATE_BOOT_ACTIVE || current_state == ROM_STATE_KERNEL_LOADING)) {
        uint16_t address = gpio_read_address_bus();
        
        if (oe && !we) {  // Read operation
            uint8_t data;
            if (rom_emulator_handle_read(address, &data)) {
                gpio_set_data_bus_direction(true);  // Set as output
                gpio_write_data_bus(data);
            }
        }
    } else if ((current_state == ROM_STATE_BOOT_ACTIVE || current_state == ROM_STATE_KERNEL_LOADING) && !rom_cs) {
        // ROM no longer selected, set data bus back to input
        gpio_set_data_bus_direction(false);
    }
    
    // Check for completion and phase transition
    if (current_state == ROM_STATE_COMPLETE) {
        printf("Kernel loading complete, transitioning to normal operation\n");
        
        // Transition to normal operation
        clock_control_set_phase(CLOCK_PHASE_NORMAL);
        gpio_put(GPIO_PICOHIRAM, 1);  // Bank out of high memory
        current_state = ROM_STATE_INACTIVE;
        
        printf("MIA banked out, clock increased to 1 MHz\n");
    }
}

bool rom_emulator_handle_read(uint16_t address, uint8_t *data) {
    if (!data) return false;
    
    // Handle reset vector ($FFFC-$FFFD maps to $3FC-$3FD in MIA space)
    if (address == ROM_RESET_VECTOR) {
        // Return low byte of boot loader start address ($E000)
        *data = 0x00;  // Low byte of $E000
        return true;
    } else if (address == ROM_RESET_VECTOR + 1) {
        // Return high byte of boot loader start address ($E000)
        *data = 0xE0;  // High byte of $E000
        return true;
    }
    
    // Handle boot loader code ($E000-$E0FF maps to $000-$0FF in MIA space)
    else if (address < KERNEL_STATUS_ADDR) {
        uint16_t offset = address - BOOTLOADER_START;
        if (offset < sizeof(bootloader_code)) {
            *data = bootloader_code[offset];
        } else {
            *data = 0xEA;  // NOP padding
        }
        return true;
    }
    
    // Handle kernel status register ($E100 maps to $100 in MIA space)
    else if (address == KERNEL_STATUS_ADDR) {
        if (current_state != ROM_STATE_KERNEL_LOADING) {
            current_state = ROM_STATE_KERNEL_LOADING;
            printf("Kernel loading started by 6502 CPU\n");
        }
        
        // Return 1 if more data available, 0 if complete
        *data = (kernel_data_pointer < sizeof(kernel_data)) ? 0x01 : 0x00;
        return true;
    }
    
    // Handle kernel data register ($E101 maps to $101 in MIA space)
    else if (address == KERNEL_DATA_ADDR) {
        if (kernel_data_pointer < sizeof(kernel_data)) {
            *data = kernel_data[kernel_data_pointer];
            kernel_data_pointer++;
            
            // Log progress periodically
            if (kernel_data_pointer % 64 == 0 || kernel_data_pointer >= sizeof(kernel_data)) {
                printf("Kernel transfer progress: %lu/%zu bytes\n", 
                       kernel_data_pointer, sizeof(kernel_data));
            }
            
            // Mark complete when all data transferred
            if (kernel_data_pointer >= sizeof(kernel_data)) {
                current_state = ROM_STATE_COMPLETE;
                printf("All kernel data transferred\n");
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

bool rom_emulator_handle_write(uint16_t address, uint8_t data) {
    (void)address;  // Unused parameter
    (void)data;     // Unused parameter
    
    // ROM emulation typically doesn't handle writes
    // Could be extended for special control registers if needed
    return false;
}

rom_state_t rom_emulator_get_state(void) {
    return current_state;
}

bool rom_emulator_is_active(void) {
    return current_state != ROM_STATE_INACTIVE;
}

uint32_t rom_emulator_get_kernel_size(void) {
    return sizeof(kernel_data);
}

uint32_t rom_emulator_get_bytes_transferred(void) {
    return kernel_data_pointer;
}