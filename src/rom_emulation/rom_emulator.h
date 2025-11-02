/**
 * ROM Emulation System for MIA
 * Provides boot-time ROM functionality for Clementina 6502 system
 */

#ifndef ROM_EMULATOR_H
#define ROM_EMULATOR_H

#include <stdint.h>
#include <stdbool.h>

// ROM memory map constants (MIA internal 1KB address space)
#define ROM_BASE_ADDRESS    0x0000  // MIA internal address space start
#define ROM_SIZE           0x0400   // 1KB ROM space (10-bit addressing)
#define ROM_RESET_VECTOR   0x03FC   // Reset vector location in MIA space ($FFFC-$FFFD)

// Boot loader addresses within ROM space (maps to $E000-$FFFF on 6502)
#define BOOTLOADER_START   0x0000   // Boot loader entry point (maps to $E000)
#define KERNEL_STATUS_ADDR 0x0100   // Status register (maps to $E100)
#define KERNEL_DATA_ADDR   0x0101   // Data register (maps to $E101)

// 6502 system addresses
#define KERNEL_LOAD_ADDRESS 0x4000  // Where kernel gets loaded in Clementina RAM

typedef enum {
    ROM_STATE_INACTIVE,
    ROM_STATE_RESET_SEQUENCE,
    ROM_STATE_BOOT_ACTIVE,
    ROM_STATE_KERNEL_LOADING,
    ROM_STATE_COMPLETE
} rom_state_t;

// Function prototypes
void rom_emulator_init(void);
void rom_emulator_process(void);
bool rom_emulator_handle_read(uint16_t address, uint8_t *data);
bool rom_emulator_handle_write(uint16_t address, uint8_t data);
rom_state_t rom_emulator_get_state(void);
void rom_emulator_start_boot_sequence(void);
bool rom_emulator_is_active(void);
uint32_t rom_emulator_get_kernel_size(void);
uint32_t rom_emulator_get_bytes_transferred(void);

#endif // ROM_EMULATOR_H