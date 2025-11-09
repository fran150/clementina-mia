# Design Document

## Overview

The MIA (Multifunction Interface Adapter) design implements a sophisticated system that combines three critical functions for the Clementina 6502 computer: programmable clock generation, ROM emulation for bootloading, and a unified indexed memory interface for accessing all MIA functionality. The design leverages the Raspberry Pi Pico 2 W's dual ARM Cortex-M33 cores and 520KB SRAM to provide a powerful memory management system with 256 independent indexes, enabling efficient access to video data, USB input, system control, and user applications while maintaining compatibility with 6502 timing requirements.

## Architecture

### System Architecture

The MIA operates in two distinct phases with different timing and processing requirements:

**Boot Phase (100 kHz):**
- Raspberry Pi PWM module generates slow clock enabling C-based ROM emulation without PIO assembly
- ARM cores have 1,330 cycles per 6502 cycle for comfortable response timing
- ROM emulation provides kernel loading functionality using C code
- Simple memory-mapped interface for boot operations

**Normal Operation Phase (1 MHz or higher):**
- Raspberry Pi PWM module generates high-speed clock for normal 6502 operation
- Indexed memory interface provides unified access to all MIA functionality
- PIO state machines handle timing-critical register access and bus protocol
- 256 independent memory indexes enable efficient data management
- Advanced video processing and Wi-Fi transmission using C code
- Real-time frame generation and transmission

### Dual-Core Architecture

**Core 0 - Real-Time System Control:**
- **Boot Phase:** ROM emulation using C code (relaxed timing at 100 kHz)
- **Normal Phase:** PIO state machine coordination and timing-critical operations
- **Bus Interface:** Direct handling of 6502 bus protocol and register access
- **Index Management:** Fast path for simple index operations and DATA_PORT access
- **Clock Generation:** PWM module control and frequency management
- **Interrupt Handling:** IRQ line management and immediate response to 6502 requests
- **DMA Operations:** Hardware-accelerated memory copying between indexes
- **Priority:** All timing-critical operations that must meet 250-500ns requirements

**Core 1 - Background Processing:**
- **Complex Index Operations:** Configuration field updates, address calculations
- **Video Processing:** Graphics data management and frame composition
- **Wi-Fi Communication:** Network transmission of video data and status
- **USB Processing:** Keyboard input handling and device management
- **Error Handling:** Non-critical error logging and recovery procedures
- **System Management:** Background tasks, diagnostics, and maintenance
- **Priority:** All non-timing-critical operations that can tolerate longer response times

**Inter-Core Communication:**
- Core 0 signals Core 1 for complex operations via interrupts
- Shared memory structures for index data and system state
- Core 1 updates index configurations atomically for Core 0 consumption
- Clear separation: Core 0 = real-time, Core 1 = background processing

## Components and Interfaces

### Hardware Interface

**GPIO Pin Mapping:**
- **GPIO 0-7**: Address bus lines A0-A7 (8-bit addressing)
- **GPIO 8-15**: Data bus lines D0-D7 for bidirectional data transfer
- **GPIO 16**: PICOHIRAM (active low) - banks MIA into high memory during ROM emulation
- **GPIO 17**: Reset line output to Clementina system (active low)
- **GPIO 18**: Write Enable (WE) input from 6502 (active low)
- **GPIO 19**: Output Enable (OE) input from 6502 (active low)
- **GPIO 20**: HIRAM Chip Select input for ROM emulation (active low)
- **GPIO 21**: IO0 Chip Select input for indexed memory interface (active low)
- **GPIO 26**: IRQ line output to 6502 CPU for interrupt notification
- **GPIO 27**: Reserved for future use
- **GPIO 28**: Clock output (PWM6A) to Clementina


**6502 Bus Interface:**
- Address decoding using GPIO 0-7 for 8-bit addressing with register mirroring
- Bidirectional data transfer via GPIO 8-15
- Control signal coordination through WE/OE inputs (active low)
- Dual chip select lines: HIRAM_CS (GPIO 20) for ROM mode, IO0_CS (GPIO 21) for indexed interface
- IRQ line (GPIO 26) for interrupt-driven event notification

**Clock Generation:**
- Raspberry Pi PWM module on GPIO 28 (PWM6A) for clock output to Clementina
- Software-controllable frequency (100 kHz boot phase, 1 MHz normal operation)
- High stability requirement (<0.1% frequency deviation)
- Hardware-based generation for precise timing
- Accessible via indexed memory interface for dynamic frequency control
- Future optimization target: 2 MHz operation with enhanced timing implementation

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
- Active during boot phase only (HIRAM_CS on GPIO 20)

**Indexed Memory Interface ($C000-$C3FF):**
- 1KB address space with 16-byte register window mirrored throughout
- Active during normal operation (IO0_CS on GPIO 21)
- Dual-window architecture for efficient data access and copying

**Window A Registers ($C000-$C007, mirrored):**
- $C000: IDX_SELECT_A - Select active index (0-255) for Window A
- $C001: DATA_PORT_A - Read/write byte at current index address with auto-step
- $C002: CFG_FIELD_SELECT_A - Select configuration field for active index
- $C003: CFG_DATA_A - Read/write selected configuration field
- $C004: COMMAND_A - Issue control commands
- $C005: STATUS - Device status bits (shared between windows)
- $C006: IRQ_CAUSE_LOW - Interrupt source identification low byte, bits 0-7 (shared between windows)
- $C007: IRQ_CAUSE_HIGH - Interrupt source identification high byte, bits 8-15 (shared between windows)

**Window B Registers ($C008-$C00F, mirrored):**
- $C008: IDX_SELECT_B - Select active index (0-255) for Window B
- $C009: DATA_PORT_B - Read/write byte at current index address with auto-step
- $C00A: CFG_FIELD_SELECT_B - Select configuration field for active index
- $C00B: CFG_DATA_B - Read/write selected configuration field
- $C00C: COMMAND_B - Issue control commands
- $C00D: STATUS - Device status bits (shared between windows)
- $C00E: IRQ_CAUSE_LOW - Interrupt source identification low byte, bits 0-7 (shared between windows)
- $C00F: IRQ_CAUSE_HIGH - Interrupt source identification high byte, bits 8-15 (shared between windows)

**Index Memory Organization:**
- 256 shared indexes (0-255) accessible from both windows
- Each index contains: current address (24-bit), default address (24-bit), step size (8-bit), flags (8-bit)
- Pre-configured indexes for system functions, video data, USB input, and user applications

### PIO State Machine Interface

**W65C02S6TPG-14 Timing Requirements:**
The MIA must comply with the specific timing requirements of the W65C02S6TPG-14 processor:

**Primary Target: 1 MHz Operation (1000ns cycle time):**
- **Address Setup Time (tADS):** 30ns before PHI2 rising edge
- **Address Hold Time (tAH):** 10ns after PHI2 falling edge  
- **Data Setup Time (tDSR):** 100ns before PHI2 falling edge (reads)
- **Data Hold Time (tDHR):** 10ns after PHI2 falling edge (reads)
- **Write Data Valid (tDSW):** 300ns after PHI2 rising edge (writes)
- **Chip Select Setup (tCSS):** 0ns (can be coincident with PHI2)
- **Output Enable Response:** Data must be valid within 200ns of OE assertion

**Future Optimization Target: 2 MHz Operation (500ns cycle time):**
- **Output Enable Response:** Data must be valid within 100ns of OE assertion
- **Write Data Valid (tDSW):** 150ns after PHI2 rising edge (writes)
- **Data Setup Time (tDSR):** 50ns before PHI2 falling edge (reads)
- Other timing requirements remain the same

**MIA Response Requirements:**
- PIO state machines provide deterministic timing for register access
- Core 0 dedicated to meeting real-time bus interface requirements
- Hybrid PIO + C implementation balances performance and functionality
- **Primary target:** 200ns response budget at 1 MHz (100ns safety margin)
- **Future goal:** 100ns response budget at 2 MHz (requires optimization)

**PIO Implementation Strategy:**
- PIO State Machine 0: Bus protocol and address decoding (GPIO 0-7, 18-21)
- PIO State Machine 1: DATA_PORT fast path for common operations
- PIO State Machine 2: Available for DMA operations or future expansion
- IO0 Chip Select (GPIO 21) triggers PIO state machine activation
- C code handles complex operations and index management
- Interrupt-driven coordination between PIO and C code

**Register Access Handling:**
- PIO monitors IO0_CS (GPIO 21) for indexed interface activation
- Address decoding via GPIO 0-7 for register selection and window detection
- Window priority: Window A takes precedence, Window B access ignored on conflicts
- Bidirectional data handling via GPIO 8-15 with automatic direction control
- **1 MHz Timing Compliance (200ns budget):**
  - **CS Detection:** PIO responds to IO0_CS assertion within 15ns
  - **OE Response:** Data valid on bus within 120ns of OE assertion (80ns safety margin)
  - **WE Handling:** Data latched within 25ns of WE deassertion
  - **Address Decode:** Complete within 40ns of address stable
- Fast path: Simple register access handled entirely in PIO (~120ns total)
- Slow path: Complex operations fall back to C code (~180ns total)
- Comfortable timing margins for reliable operation at 1 MHz
- **Future 2 MHz optimization:** Will require PIO-only fast path and enhanced caching

**W65C02S Bus Protocol Compliance (1 MHz Primary Target):**
```
Read Cycle Timing (1000ns cycle, 200ns OE budget):
1. PHI2 rises → Address becomes valid (CPU drives)
2. CS asserted → PIO detects within 15ns
3. Address decoded → Register identified within 40ns  
4. OE asserted → PIO enables data output within 25ns
5. Data valid → Must be stable within 120ns of OE (80ns margin)
6. PHI2 falls → CPU latches data, OE deasserted
7. Data hold → PIO maintains data for 10ns after PHI2 fall

Write Cycle Timing (1000ns cycle):
1. PHI2 rises → Address becomes valid (CPU drives)
2. CS asserted → PIO detects within 15ns
3. Address decoded → Register identified within 40ns
4. WE asserted → PIO prepares for data input
5. Data valid → CPU drives data after 300ns
6. WE deasserted → PIO latches data within 25ns
7. PHI2 falls → Cycle complete
```

**Timing Optimization:**
- 80% of operations use PIO fast path (IDX_SELECT, simple DATA_PORT access)
- 20% of operations use C slow path (configuration, commands, complex addressing)
- No caching between PIO and C to avoid coherency issues
- Atomic operations ensure data consistency
- All timing verified against W65C02S6TPG-14 datasheet specifications for 1 MHz operation
- Comfortable safety margins enable reliable operation and future optimization

### Indexed Memory System

**Architecture Overview:**
- 256 independent memory indexes (0-255) shared between both windows
- Each index acts as a smart pointer with automatic stepping capability
- 24-bit addressing provides access to full 16MB address space
- Dual-window design enables efficient copying between any two memory locations

**Index Structure:**
```c
typedef struct {
    uint32_t current_addr;    // 24-bit current address + 8-bit flags
    uint32_t default_addr;    // 24-bit default/base address + reserved
    uint8_t step;            // Step size (0-255 bytes)
    uint8_t reserved;        // Reserved for future use
} index_t;
```

**Index Allocation Strategy:**
- **Index 0:** System error log and status information
- **Indexes 1-15:** System/kernel reserved
- **Indexes 16-23:** Character tables (8 tables for video rendering, shared by background and sprites)
- **Indexes 32-47:** Palette banks (16 banks for video colors)
- **Indexes 48-51:** Nametables (4 tables for double buffering and scrolling)
- **Indexes 52-55:** Palette tables (4 tables for double buffering and scrolling)
- **Indexes 56:** Sprite OAM data (256 sprites)
- **Indexes 57:** Active frame control (buffer set selection for video transmission)
- **Indexes 58-63:** Reserved for video expansion
- **Indexes 64-79:** USB keyboard buffer and input devices
- **Indexes 80-95:** System control (clock, reset, IRQ mask, status)
- **Indexes 96-127:** Reserved for system expansion
- **Indexes 128-255:** User applications and general-purpose RAM

**Configuration Fields:**
- **ADDR_L/M/H (0x00-0x02):** Current address pointer (24-bit)
- **DEFAULT_L/M/H (0x03-0x05):** Default/base address (24-bit)
- **LIMIT_L/M/H (0x06-0x08):** Limit address for wrap-on-limit (24-bit)
- **STEP (0x09):** Auto-increment/decrement step size (0-255)
- **FLAGS (0x0A):** Behavior control (auto-step enable, direction, wrap-on-limit)
- **COPY_SRC_IDX (0x0B):** Source index for DMA operations
- **COPY_DST_IDX (0x0C):** Destination index for DMA operations
- **COPY_COUNT_L/H (0x0D-0x0E):** Byte count for block copy (16-bit)

**Wrap-on-Limit Feature:**
- Allows automatic reset to default address when limit is reached
- Useful for circular buffers, bounded iteration, and memory region constraints
- Enabled via FLAG_WRAP_ON_LIMIT (bit 2) in FLAGS register
- When enabled and address >= limit_addr after stepping, address resets to default_addr
- Minimal performance impact (~3-5 cycles when enabled, 0 cycles when disabled)

**Command System:**
- **Basic Commands:** RESET_INDEX, RESET_ALL, CLEAR_IRQ
- **DMA Commands:** COPY_BYTE, COPY_BLOCK with hardware acceleration
- **System Commands:** PICO_REINIT, subsystem-specific operations
- **User Commands:** Reserved range for application-specific functions

### Wi-Fi Interface

**Network Configuration:**
- Local Wi-Fi network connection
- UDP-based transmission for low latency
- Client-server architecture with MIA as server
- Automatic client discovery and connection
- Video data accessible via pre-configured indexes

## Data Models

### Indexed Memory Architecture

**Index Structure (8 bytes per index):**
```c
typedef struct {
    uint32_t current_addr;    // Bits 0-23: 24-bit current address, Bits 24-31: flags
    uint32_t default_addr;    // Bits 0-23: 24-bit default address, Bits 24-31: reserved
    uint8_t step;            // Step size (0-255 bytes)
    uint8_t reserved;        // Reserved for future use
} index_t;

// Total: 256 indexes × 8 bytes = 2KB index table
```

**Index Table Memory Layout:**
```
Index Table: 2KB (256 × 8 bytes)
- Located in MIA SRAM for fast access
- Shared between both windows
- Atomic updates to prevent corruption
- Cache-aligned for optimal performance
```

**MIA Memory Organization (256KB allocated from 520KB SRAM):**
```
0x20000000 - 0x200007FF: Index Table (2KB)
0x20000800 - 0x20004800: System Control Area (16KB)
0x20004800 - 0x20013800: Video Data Area (60KB)
0x20013800 - 0x2003C000: User Application Area (162KB)
0x2003C000 - 0x20040000: USB and I/O Buffers (16KB)
Total MIA Memory: 256KB
Remaining for Pico Runtime: 264KB (stack, heap, SDK buffers, Wi-Fi/USB stacks)
```

### Graphics Memory Architecture (Accessed via Indexes)

**Character Tables (48KB total in Video Data Area):**
```
8 tables × 256 characters × 24 bytes = 48KB
Structure per character:
- 8×8 pixel grid with 3-bit color depth
- 64 pixels × 3 bits = 192 bits = 24 bytes per character
- Shared by both background tiles and sprites (sprites reference character table entries)
- Accessible via indexes 16-23 (one index per table)
```

**Palette Banks (256 bytes total):**
```
16 banks × 8 colors × 2 bytes = 256 bytes
Structure per palette:
- 8 colors with 16-bit RGB565 format
- Shared resource, not double buffered
- Accessible via indexes 32-47 (one index per bank)
```

**Nametables (4KB total):**
```
4 buffers × 40×25 bytes = 4KB
Structure:
- 4 nametables for double buffering and scrolling support
- Buffer Set 0: Nametables 0 & 1 (two nametables for scrolling viewport)
- Buffer Set 1: Nametables 2 & 3 (two nametables for scrolling viewport)
- Each entry: 8-bit character index (0-255)
- Accessible via indexes 48-51 (one index per nametable)
```

**Palette Tables (4KB total):**
```
4 buffers × 40×25 bytes = 4KB
Structure:
- 4 palette tables matching the 4 nametables for double buffering
- Buffer Set 0: Palette Tables 0 & 1
- Buffer Set 1: Palette Tables 2 & 3
- Each entry: 4-bit palette bank selection (0-15)
- Accessible via indexes 52-55 (one index per palette table)
```

**Object Attribute Memory (1KB):**
```
256 sprites × 4 bytes = 1KB
Structure per sprite:
- Y position, tile index (references Character_Table), attributes, X position
- Sprites use 8×8 pixel size only
- Sprite graphics come from Character_Table data (no separate sprite tile storage)
- Accessible via index 56 (Sprite OAM)
- Auto-stepping enables sequential sprite access
```

**Active Frame Control:**
```
Control register accessible via index 57 (video control area)
- Selects active buffer set (0 or 1) for video transmission
- MIA transmits from active buffer set
- 6502 writes to inactive buffer set
- Buffer swap occurs on frame boundary to prevent tearing
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
9. Kernel banks out MIA (asserts PICOHIRAM), increases clock to 1 MHz or higher, and activates indexed memory interface

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
- Kernel responsible for banking out MIA, increasing clock speed to 1 MHz or higher, and initializing indexed memory interface

### USB Interface Data Model (Accessed via Indexes)

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

**USB Data Structures (in USB and I/O Buffers Area):**
```
Keyboard Buffer: 64 bytes circular buffer
- Buffer: 64 × 8-bit ASCII key codes
- Head/tail pointers for circular access
- Accessible via Index 64 with auto-stepping
- Status information accessible via Index 65

Mouse Buffer: 32 bytes (future expansion)
- Accessible via Index 66

USB Device Status: 16 bytes
- Connection status, device enumeration info
- Error codes and diagnostic information
- Accessible via Index 67
```

**Index-Based USB Access:**
```
Index 64: USB Keyboard Buffer
- Default address: Start of keyboard circular buffer
- Step size: 1 (sequential key access)
- Auto-step enabled for automatic buffer advancement

Index 65: USB Status and Control
- Device connection status
- Buffer availability flags
- Error and diagnostic information

Index 66-67: Reserved for mouse and additional USB devices
```

## Error Handling

### Indexed Memory Interface Errors

**Index Access Errors:**
- Invalid index selection (>255) - ignored, no operation performed
- Address overflow/underflow detection with INDEX_OVERFLOW IRQ
- Memory access outside valid ranges logged to Index 0 (error log)
- Automatic error recovery through index reset commands

**Bus Interface Errors:**
- Timing violations detected by PIO state machines
- Window conflict resolution (Window A priority, Window B ignored)
- Invalid register access attempts logged and ignored
- Hardware timeout detection for unresponsive 6502

**DMA Operation Errors:**
- Source/destination index validation before copy operations
- Memory boundary checking for block copy operations
- DMA_ERROR IRQ generated on invalid copy parameters
- Automatic DMA operation abort on error conditions

### Hardware Error Recovery

**Clock Generation Failures:**
- Watchdog monitoring of clock output
- Automatic fallback to safe frequency
- Error reporting via STATUS register and Index 0
- Manual recovery through clock control commands

**Memory Interface Errors:**
- PIO state machine error detection and recovery
- Automatic retry mechanisms for transient failures
- Error logging to Index 0 for debugging
- Graceful degradation with reduced functionality

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

**Index-Based Graphics Access Errors:**
- Bounds checking on character table and palette access via indexes
- Validation of sprite data accessed through Index 48
- Atomic operations for double-buffer swapping via index commands
- Recovery from invalid graphics configurations through index reset

**Video Processing Errors:**
- Frame transmission failure detection and retry
- Character table corruption detection and recovery
- Sprite collision detection with VIDEO_COLLISION IRQ
- Automatic fallback to safe graphics modes on persistent errors

### System-Wide Error Management

**Error Logging (Index 0):**
- Centralized error log accessible via Index 0
- Error codes, timestamps, and context information
- Circular buffer with automatic wraparound
- Accessible to 6502 for diagnostic purposes

**Interrupt-Driven Error Notification:**
- IRQ line (GPIO 26) signals critical errors to 6502
- 16-bit IRQ_CAUSE register identifies specific error types (0-15)
  - Low byte ($C006/$C00E): System and I/O interrupts (bits 0-7)
  - High byte ($C007/$C00F): Video interrupts (bits 8-15)
- Write-1-to-clear mechanism for selective interrupt acknowledgment
  - Writing 1 to a bit position clears that specific interrupt
  - Allows handling interrupts individually or in groups
- 16-bit IRQ_MASK register (Index 83=low, Index 84=high) enables/disables individual interrupt sources
- Each interrupt source has a corresponding mask bit (1=enabled, 0=disabled)
- All interrupts enabled by default (mask = 0xFFFF)
- CLEAR_IRQ command available to clear all interrupts at once
- IRQ line deasserts when no enabled interrupts remain pending
- Priority-based interrupt handling for multiple simultaneous errors

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

The boot phase uses the Raspberry Pi PWM module to generate a slow 100 kHz clock, enabling straightforward C implementation of ROM emulation. At this speed, Core 0 has abundant time (1,330 cycles per 6502 cycle) to:

- Decode address lines and determine ROM space access using C code
- Provide appropriate boot loader instruction bytes
- Stream kernel data with automatic pointer advancement
- Monitor completion status and coordinate phase transition to indexed interface
- No PIO assembly required due to relaxed timing constraints

### Indexed Memory Interface Implementation

The indexed memory interface uses a hybrid PIO + C architecture to meet stringent timing requirements:

**Core 0 Real-Time Operations:**
- **PIO State Machines**: Handle immediate 6502 bus protocol and register access
- **Fast Path (80% of operations)**: Simple register access completed in PIO (~132ns)
- **Slow Path (20% of operations)**: Complex operations handed to C code (~200ns)
- **Index Management**: Direct access to index table for address translation
- **DMA Operations**: Hardware-accelerated memory copying between indexes

**Core 1 Background Operations:**
- **Complex Configuration**: Multi-byte address updates and field configuration
- **Video Processing**: Graphics data management accessible via pre-configured indexes
- **USB Processing**: Keyboard input handling through Index 64
- **Error Management**: Comprehensive error logging to Index 0
- **Wi-Fi Communication**: Network transmission of video data

### Timing Optimization Strategy

**1 MHz Operation (500ns budget):**
- PIO fast path: 132ns (74% margin)
- C slow path: 200ns (60% margin)
- Comfortable timing with room for optimization

**2 MHz Operation (250ns budget):**
- PIO fast path: 132ns (47% margin)
- C slow path: 200ns (20% margin)
- Achievable with careful implementation

**Performance Optimization:**
- No caching between PIO and C to avoid coherency issues
- Atomic index updates to prevent corruption
- Cache-aligned data structures for optimal memory access
- Interrupt-driven coordination minimizes polling overhead

### Index-Based Video Processing Pipeline

The video system leverages the indexed memory interface for efficient graphics data management:

**Character Table Access (Indexes 16-31):**
- Each index points to a different character table (8×8 pixels, 3-bit color)
- Auto-stepping enables sequential character definition updates
- Direct memory access eliminates register-based bottlenecks

**Palette Management (Indexes 32-47):**
- Each index points to a different palette bank (8 colors, 16-bit RGB565)
- Dynamic color scheme updates through index-based access
- Efficient palette switching without data copying

**Sprite Processing (Index 48):**
- Single index provides access to all 256 sprites (4 bytes each)
- Auto-stepping enables sequential sprite attribute updates
- Hardware-accelerated collision detection with interrupt notification

### Memory Access Optimization

**Dual-Window Efficiency:**
- Window A and B enable simultaneous access to different memory regions
- Efficient copying: read from Window A, write to Window B
- No manual address management required for sequential operations

**DMA Acceleration:**
- Hardware block copy between any two indexes
- Up to 65535 bytes transferred without CPU intervention
- Background operation with completion interrupt notification

**Index Pre-Configuration:**
- System indexes pre-configured at startup for immediate use
- No setup overhead for accessing common data structures
- Consistent memory layout across system restarts

This indexed memory architecture provides unprecedented flexibility and performance for 6502-based systems, enabling sophisticated graphics processing, efficient data movement, and seamless integration of multiple subsystems through a unified interface.