# Requirements Document

## Introduction

The MIA (Multifunction Interface Adapter) is a Raspberry Pi Pico 2 W-based system that provides multiple critical functions for a 6502-based computer: clock generation, ROM emulation for bootloading, and video output via Wi-Fi. The system eliminates the need for physical ROM chips by intercepting reset vector access and dynamically loading the kernel into system memory.

## Glossary

- **MIA**: Multifunction Interface Adapter - the Raspberry Pi Pico 2 W acting as the multifunction chip
- **Clementina**: The main 6502-based computer system
- **Reset_Vector**: Memory addresses $FFFC-$FFFD that contain the startup address for the 6502 processor
- **Kernel**: The operating system code that needs to be loaded into system memory at startup

- **Video_Client**: External application that receives video data via Wi-Fi and renders the display
- **Character_Table**: 8x8 pixel character definitions with 3-bit color depth (8 colors per pixel), stored in MIA memory
- **Palette_Bank**: 8 colors with 16-bit color depth for character rendering
- **Nametable**: 40x25 grid specifying which characters to display at each screen position
- **Palette_Table**: 40x25 grid specifying which palette bank each character uses (4-bit values)
- **Sprite**: Movable 8x8 or 8x16 pixel graphics objects with independent positioning and attributes
- **OAM**: Object Attribute Memory containing sprite data (position, tile index, attributes, X position)
- **Sprite_Attributes**: 8-bit value containing 4-bit palette selection, priority bit, horizontal flip bit, and vertical flip bit for each sprite
- **PPU_Status**: Status register indicating sprite collision, overflow, and rendering state
- **PPU_Control**: Control register for sprite size, pattern table selection, and rendering modes
- **Scanline**: A single horizontal line of pixels on the display
- **Reset_Line**: Hardware signal that resets all chips in the Clementina system
- **PICOHIRAM**: Control line that banks the MIA in/out of the high memory region ($E000-$FFFF)
- **WE**: Write Enable signal for 6502 memory operations (active low)
- **OE**: Output Enable signal for 6502 memory operations (active low)
- **HIRAM_CS**: Chip select line on GPIO 20 for ROM emulation mode (active low)
- **IO0_CS**: Chip select line on GPIO 21 for indexed memory interface mode (active low)
- **Boot_Loader**: Small 6502 assembly program stored in MIA and executed by Clementina's CPU to copy kernel from MIA storage to RAM
- **Keyboard_Buffer**: Circular buffer storing USB keyboard input for access by Clementina
- **Key_Code**: ASCII representation of keyboard input transmitted to Clementina
- **USB_Host_Mode**: MIA operates as USB host accepting multiple devices via hub
- **USB_Device_Mode**: MIA operates as USB device for development console and debugging
- **Indexed_Memory_Interface**: Dual-window register-based interface providing access to MIA's internal memory via 256 shared indexes accessible from both windows
- **Memory_Index**: One of 256 shared pointers (0-255) that maintains current address, default address, step size, and behavior flags, accessible from either Window_A or Window_B
- **Window_A**: Primary register interface at $C000-$C007 with full functionality and access to all 256 shared indexes
- **Window_B**: Secondary register interface at $C008-$C00F with identical functionality to Window A and access to the same 256 shared indexes
- **DATA_PORT**: Register that reads/writes bytes at the current index address with automatic stepping
- **Auto_Step**: Automatic address increment or decrement after DATA_PORT access based on index configuration
- **DMA_Operation**: Hardware-accelerated memory copy between any two indexes within MIA memory space
- **IRQ_Line**: Interrupt signal from MIA to 6502 CPU on GPIO 26 for error and event notification

## Requirements

### Requirement 1

**User Story:** As a 6502 system designer, I want the MIA to generate a controllable system clock, so that I can dynamically adjust the CPU speed for different operational phases.

#### Acceptance Criteria

1. THE MIA SHALL generate a PWM clock signal for Clementina
2. WHEN Clementina is in boot phase, THE MIA SHALL output a clock frequency of 100 kHz or lower
3. WHEN the Kernel loading is complete, THE MIA SHALL increase the clock frequency to 1 MHz
4. THE MIA SHALL provide software control over clock frequency adjustment through memory-mapped registers
5. THE MIA SHALL maintain clock output stability with frequency deviation less than 0.1%

### Requirement 2

**User Story:** As a 6502 system designer, I want the MIA to emulate ROM functionality during boot, so that I can load the kernel without requiring physical ROM chips.

#### Acceptance Criteria

1. THE MIA SHALL respond to memory access in the $E000-$FFFF address range using 8 address lines (A0-A7 on GPIO 0-7) with 256-byte address space mirrored throughout the range
2. THE MIA SHALL interface with Clementina using standard 6502 memory signals: WE (Write Enable) on GPIO 18 (active low), OE (Output Enable) on GPIO 19 (active low), HIRAM chip select on GPIO 20 (active low), IO0 chip select on GPIO 21 (active low), and 8 data lines on GPIO 8-15
3. THE MIA SHALL control the PICOHIRAM line on GPIO 16 to bank itself in and out of the high memory region
4. WHEN MIA initializes, THE MIA SHALL assert the Reset_Line on GPIO 17 for minimum 5 clock cycles then release it to start the 6502 CPU
5. WHEN Clementina accesses the Reset_Vector addresses ($FFFC-$FFFD), THE MIA SHALL respond with the address of its boot loader routine in the $E000-$FFFF range
6. WHEN the 6502 CPU reads boot loader instructions from MIA, THE MIA SHALL provide hardcoded 6502 assembly code bytes that implement kernel loading logic
7. THE MIA SHALL provide a kernel status address within the 256-byte ROM space that returns 1 when more kernel data is available and 0 when kernel transfer is complete
8. THE MIA SHALL provide a kernel data address within the 256-byte ROM space that returns sequential bytes from the stored kernel and advances the internal pointer
9. THE MIA SHALL store the complete kernel binary as embedded data within the MIA firmware for byte-by-byte transfer
10. WHEN the boot loader reads from the kernel data address, THE MIA SHALL copy kernel bytes sequentially to Clementina RAM starting at address $4000
11. WHEN kernel loading is complete and PICOHIRAM is asserted by the kernel, THE MIA SHALL increase clock frequency to 1 MHz, bank out of high memory, and activate the Indexed_Memory_Interface

### Requirement 3

**User Story:** As a 6502 system designer, I want the MIA to provide video output capabilities, so that I can display graphics without dedicated video hardware.

#### Acceptance Criteria

1. THE MIA SHALL support 320x200 pixel resolution rendered from tile-based graphics data
2. THE MIA SHALL implement 8 Character_Table banks, each containing 256 character definitions of 8x8 pixels with 3-bit color depth
3. THE MIA SHALL support 16 Palette_Bank definitions, each containing 8 colors with 16-bit color depth
4. THE MIA SHALL maintain 4 Nametable buffers (2 active + 2 for double buffering), each 40x25 bytes specifying character indices
5. THE MIA SHALL maintain 2 Palette_Table buffers (1 active + 1 for double buffering), each 40x25 entries with 4-bit palette bank selection
6. THE MIA SHALL support 256 Sprite objects with configurable 8x8 or 8x16 pixel sizes
7. THE MIA SHALL maintain OAM for 256 sprites with 4 bytes per sprite (Y position, tile index, attributes, X position)
8. THE MIA SHALL transmit video data to the Video_Client via Wi-Fi connection at 30 frames per second

### Requirement 4

**User Story:** As a 6502 system designer, I want the MIA to communicate video data efficiently, so that I can achieve smooth graphics performance over Wi-Fi.

#### Acceptance Criteria

1. THE MIA SHALL transmit frame data every 33.33 milliseconds containing active Character_Table index, Nametable data, Palette_Table data, and OAM data
2. WHEN Palette_Bank definitions change, THE MIA SHALL transmit updated palette bank data to the Video_Client within 33.33 milliseconds
3. WHEN Character_Table definitions change, THE MIA SHALL transmit updated character table data to the Video_Client within 33.33 milliseconds
4. THE MIA SHALL format Nametable data as 40x25 character indices specifying which characters to display at each screen position
5. THE MIA SHALL format Palette_Table data as 40x25 entries with 4-bit palette bank selection for each character position
6. THE MIA SHALL format OAM data containing Y position, tile index, attributes, and X position for all 256 sprites
7. THE MIA SHALL support dynamic Character_Table and Palette_Bank updates without requiring system restart or initial synchronization

### Requirement 5

**User Story:** As a 6502 system designer, I want the MIA to provide a unified indexed memory interface, so that I can efficiently access all MIA functionality through a consistent register-based system.

#### Acceptance Criteria

1. THE MIA SHALL respond to I/O operations in the $C000-$C3FF address range using chip select line IO0 on GPIO 21 (active low), with 16-byte register space mirrored throughout the 1KB range
2. THE MIA SHALL provide Window_A interface at addresses $C000-$C007 (and mirrored throughout $C000-$C3FF) with full read/write functionality
3. THE MIA SHALL provide Window_B interface at addresses $C008-$C00F (and mirrored throughout $C000-$C3FF) with identical register layout to Window_A
4. WHEN both windows are accessed simultaneously, THE MIA SHALL prioritize Window_A and ignore Window_B access
5. THE MIA SHALL maintain exactly 256 shared Memory_Index entries (0-255) accessible from both windows, each containing current address, default address, step size, and behavior flags
6. THE MIA SHALL respond to Clementina memory access within 500 nanoseconds to maintain CPU timing compatibility at 1 MHz operation
7. THE MIA SHALL respond to Clementina memory access within 200 nanoseconds to maintain CPU timing compatibility at 1 MHz operation
8. THE MIA SHALL maintain data integrity during concurrent operations through atomic memory operations

### Requirement 6

**User Story:** As a 6502 system designer, I want the MIA to provide indexed memory access with automatic stepping, so that I can efficiently read and write sequential data without manual address management.

#### Acceptance Criteria

1. THE MIA SHALL provide an IDX_SELECT register at $C000 and $C008 to select the active Memory_Index (0-255) for each window from the shared pool of 256 indexes
2. THE MIA SHALL provide a DATA_PORT register at $C001 and $C009 that reads or writes one byte at the current index address
3. WHEN DATA_PORT is accessed and Auto_Step is enabled, THE MIA SHALL automatically increment or decrement the current address by the configured step size
4. THE MIA SHALL provide a CFG_FIELD_SELECT register at $C002 and $C00A to select which configuration field to access
5. THE MIA SHALL provide a CFG_DATA register at $C003 and $C00B to read or write the selected configuration field
6. THE MIA SHALL support 24-bit addressing (16MB address space) for accessing the full MIA memory range
7. THE MIA SHALL support step sizes from 0 to 255 bytes with configurable forward or backward direction
8. THE MIA SHALL provide configuration fields for current address (3 bytes), default address (3 bytes), step size (1 byte), and flags (1 byte)

### Requirement 7

**User Story:** As a 6502 system designer, I want the MIA to provide command execution and DMA capabilities, so that I can perform complex operations efficiently without manual data movement.

#### Acceptance Criteria

1. THE MIA SHALL provide a COMMAND register at $C004 and $C00C for issuing control commands
2. THE MIA SHALL support RESET_INDEX command to copy default address to current address for the active index
3. THE MIA SHALL support RESET_ALL command to reset all 256 indexes to their default addresses
4. THE MIA SHALL support COPY_BYTE command to copy one byte between any two specified indexes
5. THE MIA SHALL support COPY_BLOCK command to copy up to 65535 bytes between any two specified indexes
6. THE MIA SHALL support PICO_REINIT command to reinitialize MIA internal state without asserting the 6502 Reset_Line
7. THE MIA SHALL provide STATUS register at $C006 and $C00E indicating command completion, errors, and system state
8. THE MIA SHALL complete all commands deterministically with immediate effect or set STATUS.BUSY until completion

### Requirement 8

**User Story:** As a 6502 system designer, I want the MIA to provide interrupt-driven error handling and event notification, so that I can respond to system events and errors efficiently.

#### Acceptance Criteria

1. THE MIA SHALL provide an IRQ_Line on GPIO 26 to signal interrupts to the 6502 CPU
2. THE MIA SHALL provide an IRQ_CAUSE register at $C007 and $C00F to identify the source of interrupts
3. WHEN a memory access error occurs, THE MIA SHALL assert the IRQ_Line and set appropriate IRQ_CAUSE code
4. WHEN an index address overflow or underflow occurs, THE MIA SHALL assert the IRQ_Line and set INDEX_OVERFLOW cause
5. WHEN a DMA_Operation completes, THE MIA SHALL assert the IRQ_Line and set DMA_COMPLETE cause
6. WHEN USB keyboard data is received, THE MIA SHALL assert the IRQ_Line and set USB_KEYBOARD cause
7. THE MIA SHALL provide CLEAR_IRQ command to clear interrupt pending flags
8. THE MIA SHALL maintain error information in Memory_Index 0 accessible through the standard indexed interface

### Requirement 9

**User Story:** As a 6502 system designer, I want the MIA to provide pre-configured memory indexes for system functions, so that I can access video data, USB input, and system control without manual setup.

#### Acceptance Criteria

1. THE MIA SHALL pre-configure Memory_Index 0 to point to system error log and status information
2. THE MIA SHALL pre-configure Memory_Index 16-31 to point to Character_Table data for video rendering
3. THE MIA SHALL pre-configure Memory_Index 32-47 to point to Palette_Bank data for video colors
4. THE MIA SHALL pre-configure Memory_Index 48-63 to point to Sprite and OAM data for video objects
5. THE MIA SHALL pre-configure Memory_Index 64-79 to point to USB keyboard buffer and input device data
6. THE MIA SHALL pre-configure Memory_Index 80-95 to point to system control registers (clock, reset, status)
7. THE MIA SHALL reserve Memory_Index 128-255 for user applications and general-purpose RAM access
8. THE MIA SHALL allow reconfiguration of all pre-configured indexes through the standard CFG_FIELD_SELECT interface

### Requirement 10

**User Story:** As a 6502 system designer, I want the MIA to support dual USB operation modes through the indexed interface, so that I can use either dedicated USB devices or development console input.

#### Acceptance Criteria

1. THE MIA SHALL determine USB operation mode using a build-time configuration constant
2. WHEN configured for USB Host mode, THE MIA SHALL operate in USB Host mode using TinyUSB library
3. WHEN configured for USB Device mode, THE MIA SHALL operate in USB Device mode for development console
4. IN USB Host mode, THE MIA SHALL accept multiple USB devices connected through a USB hub
5. IN USB Host mode, THE MIA SHALL support USB keyboard and mouse input devices
6. IN USB Device mode, THE MIA SHALL provide console output for printf debugging and accept keyboard input from connected computer
7. THE MIA SHALL provide USB keyboard data through Memory_Index 64 accessible via DATA_PORT
8. WHEN a key is pressed in either mode, THE MIA SHALL store the key code in the USB keyboard buffer
9. THE MIA SHALL provide USB status information through Memory_Index 65 accessible via DATA_PORT
10. THE MIA SHALL support standard ASCII key codes for alphanumeric and special characters
11. THE MIA SHALL maintain a keyboard input buffer of at least 16 key presses to prevent data loss
12. WHEN USB keyboard buffer is accessed via DATA_PORT, THE MIA SHALL return the next key code and advance the buffer pointer automatically