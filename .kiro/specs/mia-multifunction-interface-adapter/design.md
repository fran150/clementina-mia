# Design Document

## Overview

The MIA (Multifunction Interface Adapter) design implements a sophisticated system that combines three critical functions for the Clementina 6502 computer: programmable clock generation, ROM emulation for bootloading, and advanced video output via Wi-Fi. The design leverages the Raspberry Pi Pico 2 W's dual ARM Cortex-M33 cores and 520KB SRAM to provide capabilities that emulates classic 8-bit video systems while maintaining compatibility with 6502 timing requirements.

## Architecture

### System Architecture

The MIA operates in two distinct phases with different timing and processing requirements:

**Boot Phase (100 kHz):**
- Raspberry Pi PWM module generates slow clock enabling C-based ROM emulation without PIO assembly
- ARM cores have 1,330 cycles per 6502 cycle for comfortable response timing
- ROM emulation provides kernel loading functionality using C code
- Simple memory-mapped interface for boot operations

**Normal Operation Phase (1 MHz):**
- Raspberry Pi PWM module generates high-speed clock for normal 6502 operation
- Advanced video processing and Wi-Fi transmission using C code
- PIO state machines handle timing-critical video memory-mapped I/O
- Real-time frame generation and transmission

### Dual-Core Architecture

**Core 0 - System Control:**
- ROM emulation using C code (boot phase)
- PIO state machine coordination for video I/O (normal phase)
- Clock generation using Raspberry Pi PWM module
- System reset control
- USB keyboard input processing
- Real-time response coordination

**Core 1 - Video Processing:**
- Graphics data management and frame composition using C code
- Wi-Fi transmission of video data
- Character table and palette management
- Sprite processing and collision detection
- Background processing of PIO-received video data

## Components and Interfaces

### Hardware Interface

**GPIO Pin Mapping:**
- **GPIO 0-7**: Address bus lines A0-A7
- **GPIO 8-15**: Data bus lines D0-D7 for bidirectional data transfer
- **GPIO 16**: PICOHIRAM (active low) - banks MIA into high memory during ROM emulation
- **GPIO 17**: Reset line output to Clementina system (active low)
- **GPIO 18**: Write Enable (WE) input from 6502 (active low)
- **GPIO 19**: Output Enable (OE) input from 6502 (active low)
- **GPIO 20**: ROM Emulation Chip Select input (active low)
- **GPIO 21**: Video Chip Select input (Device 4) (active low)
- **GPIO 22**: General Interface Chip Select input (Device 0) - keyboard, mouse, USB, SD cards (active low)
- **GPIO 26-27**: Reserved for future use
- **GPIO 28**: Clock output (PWM6A) to Clementina


**6502 Bus Interface:**
- Address decoding using GPIO 0-7 for 256-byte address space (8-bit addressing) with mirroring
- Bidirectional data transfer via GPIO 8-15
- Control signal coordination through WE/OE inputs
- Dual chip select lines for ROM vs Video operation modes

**Clock Generation:**
- Raspberry Pi PWM module on GPIO 28 (PWM6A) for clock output to Clementina
- Software-controllable frequency (100 kHz to 1 MHz)
- High stability requirement (<0.1% frequency deviation)
- Hardware-based generation for precise timing

**Reset Control:**
- GPIO 17 controls reset line to all Clementina system chips
- Minimum 10ms assertion time for reliable reset
- Coordinated with MIA state reinitialization

**Dual USB Interface:**
- Build-time configuration determines USB operation mode
- USB Host Mode: TinyUSB Host stack for multiple device support via hub
- USB Device Mode: TinyUSB Device stack for development console
- Keyboard buffer management for reliable input handling in both modes
- ASCII key code conversion and transmission
- Console output support for debugging in Device mode

### Memory Mapping

**ROM Emulation Region ($E000-$FFFF):**
- 256-byte address space using 8 address lines (A0-A7) with mirroring every 256 bytes
- Reset vector at $FFFC-$FFFD (mirrors to addresses $FC-$FD in MIA space)
- Boot loader code space starts at $E000 (maps to MIA $00)
- Kernel status address at $E080 (maps to MIA $80)
- Kernel data address at $E081 (maps to MIA $81)
- Address space mirrors 32 times throughout $E000-$FFFF range

**I/O Device Mapping:**

**Device 0 - General Interface ($C000-$C3FF):**
- $C000-$C0FF: USB keyboard input buffer and status
- $C100: Reset line control
- $C101-$C3FF: Reserved for future general interface functions (SD card, etc.)

**Device 4 - Video Interface ($D000-$D3FF):**
- $D000-$D0FF: Palette bank configuration (256 bytes)
- $D100-$D1FF: Character table management (256 bytes)
- $D200-$D2FF: OAM data and sprite configuration (256 bytes)
- $D300-$D304: PPU control and status registers (5 bytes)
- $D305-$D3FF: Reserved for future video functions

### PIO State Machine Interface

**Video I/O Timing Requirements:**
- 6502 memory access at 1 MHz requires ~500ns response time
- C code cannot reliably meet this timing constraint
- PIO state machines provide deterministic timing for video operations

**PIO Implementation Strategy:**
- PIO State Machine 0: Address decoding (GPIO 0-7) and read operations (GPIO 8-15)
- PIO State Machine 1: Write operations and data handling
- Video Chip Select (GPIO 21) triggers PIO state machine activation
- C code processes PIO-buffered data in background
- Interrupt-driven coordination between PIO and C code

**Memory-Mapped I/O Handling:**
- PIO monitors Video Chip Select (GPIO 21) for activation
- Address decoding via GPIO 0-7 for video I/O region ($C000-$DFFF)
- Bidirectional data handling via GPIO 8-15
- WE/OE signal coordination (GPIO 18-19)
- Data buffering for C code processing
- Status signaling between PIO and ARM cores

### Wi-Fi Interface

**Network Configuration:**
- Local Wi-Fi network connection
- UDP-based transmission for low latency
- Client-server architecture with MIA as server
- Automatic client discovery and connection

## Data Models

### Graphics Memory Architecture

**Character Tables (48KB total):**
```
8 tables × 256 characters × 64 pixels × 3 bits = 48KB
Structure per character:
- 8×8 pixel grid
- 3-bit color index per pixel (0-7 colors)
- 64 pixels × 3 bits = 192 bits = 24 bytes per character
```

**Palette Banks (256 bytes total):**
```
16 banks × 8 colors × 2 bytes = 256 bytes
Structure per palette:
- 8 colors with 16-bit color depth
- RGB565 format: 5 bits red, 6 bits green, 5 bits blue
```

**Nametables (4KB total):**
```
4 buffers × 40×25 bytes = 4KB
Structure:
- 2 active nametables (current display)
- 2 double-buffer nametables (next frame)
- Each entry: 8-bit character index (0-255)
```

**Palette Tables (1KB total):**
```
2 buffers × 40×25 × 4 bits = 1KB
Structure:
- 1 active palette table
- 1 double-buffer palette table
- Each entry: 4-bit palette bank index (0-15)
```

**Object Attribute Memory (1KB):**
```
256 sprites × 4 bytes = 1KB
Structure per sprite:
- Byte 0: Y position (0-199)
- Byte 1: Tile index (0-255, references character table)
- Byte 2: Attributes (4-bit palette, priority, H/V flip)
- Byte 3: X position (0-319)
```

### Boot Sequence Data Model

**Boot Sequence Flow:**
1. MIA configures internal systems and asserts Reset_Line for minimum 5 cycles
2. MIA starts PWM generation at 100 kHz and releases Reset_Line
3. 6502 CPU reads reset vector ($FFFC-$FFFD) from MIA ROM space
4. MIA responds with boot loader entry address in $E000-$FFFF range
5. CPU executes boot loader code provided by MIA ROM emulation
6. Boot loader implements kernel copying loop using two fixed addresses
7. Kernel data (loaded from `kernel.bin` at build time) copied byte-by-byte to RAM starting at $4000
8. Boot loader jumps to $4000 to start kernel execution
9. Kernel banks out MIA (asserts PICOHIRAM) and increases clock to 1 MHz

**Kernel Development Workflow:**
1. Develop kernel code using 6502 assembler/compiler
2. Generate `kernel.bin` binary file
3. Place `kernel.bin` in MIA project root directory
4. Build MIA firmware (CMake automatically embeds kernel data)
5. Upload `mia.uf2` to Raspberry Pi Pico

**Boot Loader Structure:**
- Minimal 6502 assembly routine (~30 bytes) stored in MIA flash
- Implements simple copying loop with two memory-mapped addresses:
  - Status address ($E100): Returns 1 if more data available, 0 when complete
  - Data address ($E101): Returns next kernel byte and advances internal pointer
- Transfers control to kernel entry point at $4000

**Boot Loader Assembly Code:**
```assembly
; Clementina 6502 Kernel Loader
; This code will be stored in MIA flash and provided as ROM data
; Memory layout: Boot loader starts at $E000, Reset vector at $FFFC-$FFFD

.org $E000

; === Kernel Loader Entry Point ===
KERNEL_LOADER:
    ; Initialize system
    SEI                     ; Disable interrupts during loading
    CLD                     ; Clear decimal mode
    
    ; Set up destination pointer for kernel loading
    LDA #$00               ; Low byte of $4000 (kernel start address)
    STA $00                ; Store in zero page pointer (low byte)
    LDA #$40               ; High byte of $4000
    STA $01                ; Store in zero page pointer (high byte)
    
    ; Initialize Y register as offset
    LDY #$00
    
; === Main Kernel Loading Loop ===
LOAD_LOOP:
    ; Check if more kernel data is available
    LDA $E080              ; Read status address (mapped to MIA status register)
    BEQ LOAD_COMPLETE      ; If 0, loading is complete
    
    ; Read next kernel byte
    LDA $E081              ; Read data address (mapped to MIA data register)
    STA ($00),Y            ; Store byte at destination address
    
    ; Advance destination pointer
    INY                    ; Increment offset
    BNE LOAD_LOOP          ; If Y didn't wrap, continue
    
    ; Handle page boundary crossing
    INC $01                ; Increment high byte of destination
    JMP LOAD_LOOP          ; Continue loading
    
; === Loading Complete ===
LOAD_COMPLETE:
    ; Jump directly to kernel entry point
    ; Kernel will handle banking out MIA and speed increase
    JMP $4000              ; Start kernel execution
    
; === Reset and Interrupt Vectors ===
.org $FFFA
    .word $0000            ; NMI vector (unused)
    .word KERNEL_LOADER    ; Reset vector - points to our loader
    .word $0000            ; IRQ/BRK vector (unused during boot)
```

**MIA ROM Memory Map (256-byte space, mirrored):**
- `$E000` (MIA $00): Kernel loader entry point
- `$E080` (MIA $80): Status register (1 = more data available, 0 = transfer complete)
- `$E081` (MIA $81): Data register (returns next kernel byte, advances pointer)
- `$FFFC-$FFFD` (MIA $FC-$FD): Reset vector pointing to `$E000`

**Kernel Storage:**
- Complete kernel binary loaded from `kernel.bin` file at compile time
- Binary data automatically converted to C array during build process
- Sequential byte streaming via memory-mapped data address ($E101)
- Automatic internal pointer advancement on each read
- Kernel development independent of MIA firmware - only requires updating `kernel.bin`
- Kernel responsible for banking out MIA and increasing clock speed to 1 MHz

### USB Interface Data Model

**Dual USB Mode Architecture:**
```
Build-time Configuration:
- CONFIG_USB_HOST: Compile for USB Host Mode
- CONFIG_USB_DEVICE: Compile for USB Device Mode
```

**USB Host Mode:**
```
TinyUSB Host Stack Configuration:
- Multiple device support via USB hub
- Keyboard device class support
- Mouse device class support (future expansion)
- Device enumeration and management
```

**USB Device Mode:**
```
TinyUSB Device Stack Configuration:
- CDC (Communication Device Class) for console
- Printf output redirection to USB console
- Keyboard input from connected computer
- Development and debugging support
```

**Keyboard Buffer (Both Modes):**
```
16-byte circular buffer for key codes
Structure:
- Buffer: 16 × 8-bit ASCII key codes
- Head pointer: Current write position
- Tail pointer: Current read position
- Status flags: Buffer full, buffer empty, key available
```

**General Interface Registers:**
```
$C000: Keyboard data register (read: next key code)
$C001: Keyboard status register (bit 0: key available, bit 7: buffer full)
$C002: Keyboard buffer head pointer (read-only)
$C003: Keyboard buffer tail pointer (read-only)
$C004: USB mode status register (bit 0: current mode, bit 1: device connected)
$C100: Reset line control register
```

## Error Handling

### Hardware Error Recovery

**Clock Generation Failures:**
- Watchdog monitoring of clock output
- Automatic fallback to safe frequency
- Error reporting via status registers
- Manual recovery through reset sequence

**Memory Interface Errors:**
- Timeout detection for 6502 operations
- Automatic retry mechanisms
- Error logging for debugging
- Graceful degradation of functionality

### Wi-Fi Communication Errors

**Connection Management:**
- Automatic reconnection on link failure
- Client timeout detection and recovery
- Bandwidth adaptation based on network conditions
- Fallback to reduced frame rate on congestion

**Data Transmission Errors:**
- Frame sequence numbering for lost packet detection
- Selective retransmission of critical updates
- Compression for bandwidth optimization
- Priority queuing (frame data > resource updates)

### Graphics System Errors

**Memory Corruption Protection:**
- Bounds checking on all graphics memory access
- Atomic operations for double-buffer swapping
- Validation of character and palette indices
- Recovery from invalid sprite configurations

**Collision Detection Errors:**
- Hardware-accelerated sprite overlap detection
- Configurable collision sensitivity
- Status register flags for software handling
- Performance monitoring and optimization

## Testing Strategy

### Unit Testing Approach

**Core Functionality Tests:**
- Clock generation accuracy and stability testing
- ROM emulation timing verification
- Memory-mapped I/O response time validation
- Wi-Fi transmission throughput measurement

**Graphics System Tests:**
- Character table rendering verification
- Palette switching accuracy testing
- Sprite positioning and collision detection
- Frame rate consistency measurement

### Integration Testing

**6502 Interface Testing:**
- Boot sequence end-to-end verification
- Memory timing compatibility testing
- Reset sequence coordination validation
- Real-world kernel loading scenarios

**Video Client Integration:**
- Frame data transmission accuracy
- Resource update synchronization
- Network latency impact assessment
- Multi-client support validation

### Performance Testing

**Timing Requirements Validation:**
- 500ns memory access response verification
- 33.33ms frame transmission timing
- 1MHz clock stability measurement
- Boot sequence timing optimization

**Resource Utilization Testing:**
- Memory usage profiling and optimization
- CPU core load balancing verification
- Wi-Fi bandwidth utilization measurement
- Power consumption analysis

### Hardware-in-Loop Testing

**Real Hardware Validation:**
- Clementina computer integration testing
- Video client display quality assessment
- Network infrastructure compatibility
- Environmental stress testing (temperature, interference)

**Regression Testing:**
- Automated test suite for core functionality
- Performance benchmark tracking
- Compatibility testing across Wi-Fi standards
- Long-duration stability testing

## Implementation Considerations

### Boot Phase Implementation

The boot phase uses the Raspberry Pi PWM module to generate a slow 100 kHz clock, enabling straightforward C implementation of ROM emulation. At this speed, the ARM cores have abundant time (1,330 cycles per 6502 cycle) to:

- Decode address lines and determine ROM space access using C code
- Provide appropriate boot loader instruction bytes
- Stream kernel data with automatic pointer advancement
- Monitor completion status and coordinate phase transition
- No PIO assembly required due to relaxed timing constraints

### Video I/O Implementation

The video I/O phase uses PIO state machines to meet the stringent 500ns response requirements at 1 MHz operation:

- **PIO State Machines**: Handle immediate 6502 bus interface timing
- **C Code Processing**: Manages complex graphics operations in background
- **Interrupt Coordination**: PIO signals C code for data processing
- **PWM Clock Generation**: Raspberry Pi PWM module provides stable 1 MHz clock
- **Hybrid Architecture**: Combines PIO timing precision with C code flexibility

### Video Processing Pipeline

The video system uses a tile-based architecture that efficiently balances memory usage with transmission bandwidth:

- Character tables provide rich 3-bit color depth (8 colors per pixel)
- Palette banks enable dynamic color schemes with 16-bit depth
- Double-buffered nametables prevent visual artifacts
- Sprite system supports 256 objects with full attribute control

### Wi-Fi Transmission Optimization

The two-part transmission system optimizes bandwidth usage:

- Frame data (2.5KB) transmitted every 33.33ms for consistent display
- Resource updates sent only when needed, reducing average bandwidth
- No initial synchronization required, enabling dynamic content loading
- Compression opportunities for character and palette data

This design provides a robust, high-performance solution that significantly exceeds the capabilities of classic 8-bit video systems while maintaining compatibility with 6502 timing requirements and leveraging modern Wi-Fi infrastructure for flexible display options.