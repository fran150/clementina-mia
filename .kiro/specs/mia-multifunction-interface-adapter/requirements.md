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
- **WE**: Write Enable signal for 6502 memory operations
- **OE**: Output Enable signal for 6502 memory operations
- **Boot_Loader**: Small 6502 assembly program stored in MIA and executed by Clementina's CPU to copy kernel from MIA storage to RAM
- **Keyboard_Buffer**: Circular buffer storing USB keyboard input for access by Clementina
- **Key_Code**: ASCII representation of keyboard input transmitted to Clementina
- **USB_Host_Mode**: MIA operates as USB host accepting multiple devices via hub (GPIO 28 jumper connected)
- **USB_Device_Mode**: MIA operates as USB device for development console and debugging (GPIO 28 jumper disconnected)
- **Mode_Jumper**: GPIO 29 jumper setting that determines USB operation mode

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
2. THE MIA SHALL interface with Clementina using standard 6502 memory signals: WE (Write Enable) on GPIO 18, OE (Output Enable) on GPIO 19, and 8 data lines on GPIO 8-15
3. THE MIA SHALL control the PICOHIRAM line on GPIO 16 to bank itself in and out of the high memory region
4. WHEN MIA initializes, THE MIA SHALL assert the Reset_Line on GPIO 17 for minimum 5 clock cycles then release it to start the 6502 CPU
5. WHEN Clementina accesses the Reset_Vector addresses ($FFFC-$FFFD), THE MIA SHALL respond with the address of its boot loader routine in the $E000-$FFFF range
6. WHEN the 6502 CPU reads boot loader instructions from MIA, THE MIA SHALL provide hardcoded 6502 assembly code bytes that implement kernel loading logic
7. THE MIA SHALL provide a kernel status address within the 256-byte ROM space that returns 1 when more kernel data is available and 0 when kernel transfer is complete
8. THE MIA SHALL provide a kernel data address within the 256-byte ROM space that returns sequential bytes from the stored kernel and advances the internal pointer
9. THE MIA SHALL store the complete kernel binary as embedded data within the MIA firmware for byte-by-byte transfer
10. WHEN the boot loader reads from the kernel data address, THE MIA SHALL copy kernel bytes sequentially to Clementina RAM starting at address $4000
11. WHEN kernel loading is complete and PICOHIRAM is asserted by the kernel, THE MIA SHALL increase clock frequency to 1 MHz and bank out of high memory

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

**User Story:** As a 6502 system designer, I want the MIA to handle memory-mapped I/O efficiently, so that the 6502 system can control video operations seamlessly.

#### Acceptance Criteria

1. THE MIA SHALL respond to video I/O operations in the $C000-$DFFF address range using a dedicated chip select line on GPIO 21
2. WHEN Clementina writes to Nametable or Palette_Table addresses, THE MIA SHALL update the corresponding graphics data within 1 microsecond
3. THE MIA SHALL provide memory-mapped registers at addresses $D000-$D0FF for Palette bank configuration and selection
4. THE MIA SHALL provide memory-mapped registers at addresses $D100-$D1FF for Character_Table bank updates and selection
5. THE MIA SHALL provide memory-mapped registers at addresses $D200-$D2FF for OAM data and Sprite configuration
6. THE MIA SHALL provide memory-mapped registers for Character_Table bank switching and Palette bank switching
7. THE MIA SHALL respond to Clementina memory access within 500 nanoseconds to maintain CPU timing compatibility
8. THE MIA SHALL maintain data integrity during concurrent video operations and memory access through atomic memory operations

### Requirement 6

**User Story:** As a 6502 system designer, I want the MIA to provide comprehensive sprite control and status registers, so that I can efficiently manage sprite operations and detect collision events.

#### Acceptance Criteria

1. THE MIA SHALL provide a PPU_Control register at address $D300 for sprite size selection and rendering mode configuration
2. THE MIA SHALL provide a PPU_Status register at address $D301 for sprite collision detection and rendering state
3. THE MIA SHALL provide an OAM address register at address $D302 for selecting which sprite to access
4. THE MIA SHALL provide an OAM data register at address $D303 for reading and writing individual sprite attributes
5. THE MIA SHALL provide an OAM DMA register at address $D304 for fast transfer of sprite data from Clementina memory
6. WHEN two sprites overlap, THE MIA SHALL set collision flags in the PPU_Status register
7. WHEN more than 16 sprites appear on the same scanline, THE MIA SHALL set overflow flag in the PPU_Status register
8. THE MIA SHALL support Sprite_Attributes including 4-bit palette selection, priority bit, and horizontal/vertical flip bits

### Requirement 7

**User Story:** As a 6502 system designer, I want the MIA to control the system reset line, so that I can perform software-controlled system resets without manual intervention.

#### Acceptance Criteria

1. THE MIA SHALL control the Reset_Line signal to all chips in the Clementina system
2. WHEN a software reset command is received, THE MIA SHALL assert the Reset_Line for a minimum of 10 milliseconds
3. THE MIA SHALL provide a memory-mapped register at address $C100 for Reset_Line control
4. WHEN the Reset_Line is asserted, THE MIA SHALL reinitialize its own ROM emulation state
5. THE MIA SHALL release the Reset_Line and resume normal operation after reset sequence completion

### Requirement 8

**User Story:** As a 6502 system designer, I want the MIA to support dual USB operation modes, so that I can use either dedicated USB devices or development console input for the Clementina system.

#### Acceptance Criteria

1. THE MIA SHALL determine USB operation mode using a build-time configuration constant
2. WHEN configured for USB Host mode, THE MIA SHALL operate in USB Host mode using TinyUSB library
3. WHEN configured for USB Device mode, THE MIA SHALL operate in USB Device mode for development console
4. IN USB Host mode, THE MIA SHALL accept multiple USB devices connected through a USB hub
5. IN USB Host mode, THE MIA SHALL support USB keyboard and mouse input devices
6. IN USB Device mode, THE MIA SHALL provide console output for printf debugging and accept keyboard input from connected computer
7. THE MIA SHALL provide memory-mapped registers at addresses $C000-$C0FF for keyboard data and status in both modes
8. WHEN a key is pressed in either mode, THE MIA SHALL store the key code in a keyboard buffer accessible to Clementina
9. THE MIA SHALL provide a keyboard status register indicating buffer availability and key press events
10. THE MIA SHALL support standard ASCII key codes for alphanumeric and special characters
11. THE MIA SHALL maintain a keyboard input buffer of at least 16 key presses to prevent data loss
12. WHEN Clementina reads from the keyboard data register, THE MIA SHALL return the next key code and advance the buffer pointer