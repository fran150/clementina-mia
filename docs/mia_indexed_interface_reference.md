# MIA Indexed Memory Interface Reference

## 6502 Address Map (Multi-Window Interface)

### Memory Map Overview

**6502 Address Space:** $C000-$C3FF (1KB total)
**MIA Address Space:** $00-$FF (256 bytes, seen on GPIO 0-7)
**Mirroring:** 256-byte pattern repeats 4 times throughout 1KB range

| 6502 Address Range | MIA Sees | Window/Space | Description |
|-------------------|----------|--------------|-------------|
| $C000-$C00F | $00-$0F | Window A | 16 registers (5 active, 11 reserved) |
| $C010-$C01F | $10-$1F | Window B | 16 registers (5 active, 11 reserved) |
| $C020-$C02F | $20-$2F | Window C | 16 registers (5 active, 11 reserved) |
| $C030-$C03F | $30-$3F | Window D | 16 registers (5 active, 11 reserved) |
| $C040-$C07F | $40-$7F | Reserved | Future windows E-H (64 bytes) |
| $C080-$C0EF | $80-$EF | Reserved | Future shared registers (112 bytes) |
| $C0F0-$C0FF | $F0-$FF | Shared | Active shared registers (16 bytes) |
| $C100-$C1FF | $00-$FF | (Mirror) | Repeats $C000-$C0FF pattern |
| $C200-$C2FF | $00-$FF | (Mirror) | Repeats $C000-$C0FF pattern |
| $C300-$C3FF | $00-$FF | (Mirror) | Repeats $C000-$C0FF pattern |

### Window Registers (All Windows A-D)

Each window has identical register layout at offsets +0 through +15:

| Offset | 6502 Addr (A/B/C/D) | MIA Sees | Access | Function |
|--------|---------------------|----------|--------|----------|
| +0 | $C000/$C010/$C020/$C030 | $x0 | R/W | IDX_SELECT - Select active index (0-255) |
| +1 | $C001/$C011/$C021/$C031 | $x1 | R/W | DATA_PORT - Read/write with auto-step |
| +2 | $C002/$C012/$C022/$C032 | $x2 | R/W | CFG_FIELD_SELECT - Select config field |
| +3 | $C003/$C013/$C023/$C033 | $x3 | R/W | CFG_DATA - Read/write config field |
| +4 | $C004/$C014/$C024/$C034 | $x4 | W | COMMAND - Issue control commands |
| +5-15 | $C005-$C00F/etc. | $x5-$xF | - | Reserved for future use |

### Shared Registers ($C0F0-$C0FF)

| 6502 Address | MIA Sees | Access | Function |
|--------------|----------|--------|----------|
| $C0F0 | $F0 | R | DEVICE_STATUS - Global device status |
| $C0F1 | $F1 | R/W | IRQ_CAUSE_LOW - Interrupt source (bits 0-7, write-1-to-clear) |
| $C0F2 | $F2 | R/W | IRQ_CAUSE_HIGH - Interrupt source (bits 8-15, write-1-to-clear) |
| $C0F3 | $F3 | R/W | IRQ_MASK_LOW - Interrupt mask (bits 0-7) |
| $C0F4 | $F4 | R/W | IRQ_MASK_HIGH - Interrupt mask (bits 8-15) |
| $C0F5 | $F5 | R/W | IRQ_ENABLE - Global interrupt enable |
| $C0F6 | $F6 | R | DEVICE_ID_LOW - Device ID low byte ('M' = 0x4D) |
| $C0F7 | $F7 | R | DEVICE_ID_HIGH - Device ID high byte ('I' = 0x49) |
| $C0F8-$C0FF | $F8-$FF | - | Reserved shared registers |

**Control Signals:**
- **IO0_CS:** GPIO 21 (active low) - Chip select for indexed memory interface
- **WE:** GPIO 18 (active low) - Write Enable
- **OE:** GPIO 19 (active low) - Output Enable
- **IRQ:** GPIO 26 - Interrupt line to 6502 CPU

**Shared Resources:**
- All windows access the same pool of 256 indexes (0-255)
- Each window maintains independent active index selection
- Shared registers accessible from any address in the 1KB range (via mirroring)

## Index Allocation Ranges

| Index Range | Purpose | Description |
|-------------|---------|-------------|
| 0 | System Error/Status | Pre-configured to system error log and status area |
| 1-15 | System/Kernel | Reserved for kernel and system use |
| 16-23 | Character Tables | Video character table access (8 tables, shared by background and sprites) |
| 32-47 | Palette Banks | Video palette bank access (16 banks, shared resource) |
| 48-51 | Nametables | Background nametable data (4 tables for double buffering and scrolling) |
| 52-55 | Palette Tables | Palette selection tables (4 tables for double buffering and scrolling) |
| 56 | Sprite OAM | Sprite Object Attribute Memory (256 sprites × 4 bytes, 8×8 pixels) |
| 57 | Active Frame Control | Buffer set selection (0 or 1) for video transmission |
| 58-63 | Video Reserved | Reserved for video expansion |
| 64-79 | USB/Input | USB keyboard buffer and input devices |
| 80-95 | System Control | Clock control, reset control, system registers |
| 96-127 | Reserved System | Reserved for future system expansion |
| 128-255 | User/Application | Available for user applications and general RAM |

## Pre-configured Index Targets

| Index | Target Memory Type | Description |
|-------|-------------------|-------------|
| 0 | System Error Log | Error details and system status information |
| 16-23 | Character Tables 0-7 | 8 tables × 256 characters × 24 bytes (8×8 pixels, 3-bit color) |
| 32-47 | Palette Banks 0-15 | 16 banks × 8 colors × 2 bytes (RGB565) |
| 48-51 | Nametables 0-3 | 4 tables × 40×25 bytes (character indices for double buffering/scrolling) |
| 52-55 | Palette Tables 0-3 | 4 tables × 40×25 bytes (palette bank selection for double buffering/scrolling) |
| 56 | Sprite OAM | 256 sprites × 4 bytes (Y, tile index from character table, attributes, X) |
| 57 | Active Frame Control | Buffer set selection (0 or 1) for video transmission |
| 64 | USB Keyboard Buffer | Circular keyboard input buffer |
| 65 | USB Status | USB device status and control |
| 80 | Clock Control | PWM frequency control registers |
| 81 | Reset Control | System reset control |

## CFG_FIELD_SELECT Values

### Index Configuration Fields
| Field ID | Name | Width | Description |
|----------|------|-------|-------------|
| 0x00 | ADDR_L | 8-bit | Low byte of current address pointer |
| 0x01 | ADDR_M | 8-bit | Mid byte of current address pointer |
| 0x02 | ADDR_H | 8-bit | High byte of current address pointer (24-bit total) |
| 0x03 | DEFAULT_L | 8-bit | Low byte of default/base address |
| 0x04 | DEFAULT_M | 8-bit | Mid byte of default/base address |
| 0x05 | DEFAULT_H | 8-bit | High byte of default/base address |
| 0x06 | LIMIT_L | 8-bit | Low byte of limit address for wrap-on-limit |
| 0x07 | LIMIT_M | 8-bit | Mid byte of limit address for wrap-on-limit |
| 0x08 | LIMIT_H | 8-bit | High byte of limit address for wrap-on-limit |
| 0x09 | STEP | 8-bit | Step size (0-255 bytes) for auto-increment |
| 0x0A | FLAGS | 8-bit | Index behavior control flags |

### DMA Configuration Fields
| Field ID | Name | Width | Description |
|----------|------|-------|-------------|
| 0x0B | COPY_SRC_IDX | 8-bit | Source index for copy operations |
| 0x0C | COPY_DST_IDX | 8-bit | Destination index for copy operations |
| 0x0D | COPY_COUNT_L | 8-bit | Low byte of copy byte count |
| 0x0E | COPY_COUNT_H | 8-bit | High byte of copy byte count (16-bit total) |
| 0x0F | RESERVED | 8-bit | Reserved for future expansion |

## FLAGS Register Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | AUTO_STEP | 0=Manual stepping, 1=Auto-step after DATA_PORT access |
| 1 | DIRECTION | 0=Forward/increment, 1=Backward/decrement |
| 2 | WRAP_ON_LIMIT | 0=Disabled, 1=Wrap to default address when reaching limit address |
| 3-7 | RESERVED | Reserved for future use |

## COMMAND Codes

### Basic Commands
| Code | Name | Action |
|------|------|--------|
| 0x00 | NOP | No operation |
| 0x01 | RESET_INDEX | Copy DEFAULT_ADDR → CURRENT_ADDR for active index |
| 0x02 | RESET_ALL | Copy DEFAULT_ADDR → CURRENT_ADDR for all indexes |
| 0x03 | LOAD_DEFAULT | Copy specified address → DEFAULT_ADDR for active index |
| 0x04 | SET_DEFAULT_TO_ADDR | Copy CURRENT_ADDR → DEFAULT_ADDR for active index |
| 0x05 | CLEAR_IRQ | Clear interrupt pending flags |

### DMA/Copy Commands
| Code | Name | Action |
|------|------|--------|
| 0x06 | COPY_BYTE | Copy one byte from source index to destination index |
| 0x07 | COPY_BLOCK | Copy N bytes from source index to destination index |
| 0x08 | SET_COPY_SRC | Set source index for copy operations |
| 0x09 | SET_COPY_DST | Set destination index for copy operations |
| 0x0A | SET_COPY_COUNT | Set byte count for COPY_BLOCK operation |

### System Commands
| Code | Name | Action |
|------|------|--------|
| 0x10 | PICO_REINIT | Reinitialize MIA internal state (soft reset) |
| 0x20-0x2F | VIDEO_CMD | Video subsystem commands |
| 0x30-0x3F | USB_CMD | USB subsystem commands |
| 0x40-0x4F | CLOCK_CMD | Clock control commands |
| 0xF0-0xFF | USER_CMD | User-defined commands |

## DEVICE_STATUS Register Bits ($C0F0)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | BUSY | Command in progress |
| 1 | IRQ_PENDING | Interrupt pending |
| 2 | MEMORY_ERROR | Invalid memory access occurred (address ≥ 256KB) |
| 3 | INDEX_OVERFLOW | Reserved (not currently used) |
| 4 | USB_DATA_READY | USB keyboard data available |
| 5 | VIDEO_FRAME_READY | Video frame ready for transmission |
| 6 | DMA_ACTIVE | DMA/copy operation in progress |
| 7 | SYSTEM_READY | MIA fully initialized and ready |

## IRQ_CAUSE Codes ($C0F1-$C0F2)

| Code | Name | Description | Mask Register | Bit |
|------|------|-------------|---------------|-----|
| 0x0000 | NO_IRQ | No interrupt pending | N/A | N/A |
| 0x0001 | MEMORY_ERROR | Invalid memory access (address ≥ 256KB) | $C0F3 (Low) | 0 |
| 0x0002 | INDEX_OVERFLOW | Reserved (not currently used) | $C0F3 (Low) | 1 |
| 0x0004 | DMA_COMPLETE | DMA/copy operation completed | $C0F3 (Low) | 2 |
| 0x0008 | DMA_ERROR | DMA/copy operation failed | $C0F3 (Low) | 3 |
| 0x0010 | USB_KEYBOARD | Keyboard data received | $C0F3 (Low) | 4 |
| 0x0020 | USB_DEVICE_CHANGE | USB device connected/disconnected | $C0F3 (Low) | 5 |
| 0x0040 | RESERVED | Reserved for future use | $C0F3 (Low) | 6 |
| 0x0080 | RESERVED | Reserved for future use | $C0F3 (Low) | 7 |
| 0x0100 | VIDEO_FRAME_COMPLETE | Video frame transmission complete | $C0F4 (High) | 0 |
| 0x0200 | VIDEO_COLLISION | Sprite collision detected | $C0F4 (High) | 1 |
| 0x0400-0x8000 | RESERVED | Reserved for future use | $C0F4 (High) | 2-7 |

**Note:** IRQ cause codes are bit masks (power of 2 values) that directly correspond to bits in the 16-bit IRQ_MASK register for efficient masking with a single AND operation.

### Memory Error Behavior

**MEMORY_ERROR** is triggered when attempting to access an invalid memory address:

- **When it occurs:** During read or write operations (DATA_READ, DATA_WRITE, or their no-step variants)
- **Invalid addresses:** Any address ≥ 0x040000 (beyond the 256KB MIA memory range)
- **Behavior on error:**
  - Read operations return 0x00
  - Write operations are skipped (no memory modification)
  - MEMORY_ERROR status bit is set
  - IRQ_MEMORY_ERROR interrupt is triggered (if enabled)
  - Index address is NOT modified

**Important:** Address validation occurs at access time, not when configuring indexes. You can set an index to an invalid address without error, but attempting to read or write will trigger MEMORY_ERROR.

**Example scenario:**
```assembly
; Configure index with invalid address (no error yet)
LDA #128        ; Select user index
STA $C000       ; Window A
LDA #$08        ; Address high byte = 0x08 (beyond 256KB)
STA $C002       ; CFG_ADDR_H

; Attempt to read - triggers MEMORY_ERROR
LDA $C010       ; DATA_READ - returns 0x00, sets error flag

; Check for error
LDA $C0F0       ; Read DEVICE_STATUS
AND #$04        ; Check MEMORY_ERROR bit
BNE error_handler
```

**Auto-stepping behavior:** If auto-step causes an index to advance beyond valid memory, the address is still updated. The error will be detected on the next read/write attempt.

## IRQ_MASK Registers (16-bit, $C0F3-$C0F4)

### $C0F3: IRQ_MASK_LOW (Bits 0-7)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | MEMORY_ERROR | Enable/disable memory error interrupts (invalid address access) |
| 1 | INDEX_OVERFLOW | Reserved (not currently used) |
| 2 | DMA_COMPLETE | Enable/disable DMA completion interrupts |
| 3 | DMA_ERROR | Enable/disable DMA error interrupts |
| 4 | USB_KEYBOARD | Enable/disable USB keyboard interrupts |
| 5 | USB_DEVICE_CHANGE | Enable/disable USB device change interrupts |
| 6 | RESERVED | Reserved for future use |
| 7 | RESERVED | Reserved for future use |

### $C0F4: IRQ_MASK_HIGH (Bits 8-15)

| Bit | Name | Description |
|-----|------|-------------|
| 0 (bit 8) | VIDEO_FRAME | Enable/disable video frame complete interrupts |
| 1 (bit 9) | VIDEO_COLLISION | Enable/disable sprite collision interrupts |
| 2-7 (bits 10-15) | RESERVED | Reserved for future use |

**Default Value:** 0xFFFF (all interrupts enabled)

**Usage:** Write 1 to enable an interrupt source, 0 to disable it. Only enabled interrupts will assert the IRQ line.

## IRQ_ENABLE Register ($C0F5)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | GLOBAL_IRQ_ENABLE | 0=All interrupts disabled, 1=Interrupts enabled (subject to mask) |
| 1-7 | RESERVED | Reserved for future use |

**Default Value:** 0x01 (interrupts enabled)

## DEVICE_ID Registers ($C0F6-$C0F7)

| Register | Value | Description |
|----------|-------|-------------|
| $C0F6 | 0x4D | ASCII 'M' - MIA identifier (low byte) |
| $C0F7 | 0x49 | ASCII 'I' - MIA identifier (high byte) |

**Usage:** Read-only registers for device identification. Software can verify it's communicating with MIA by reading these registers.

## Interrupt Acknowledgment

The IRQ_CAUSE register acts as a **pending interrupt register** where each bit represents a pending interrupt:

1. **Interrupt occurs:** MIA sets the corresponding bit in IRQ_CAUSE and asserts IRQ line (if enabled in mask and global enable)
2. **6502 reads IRQ_CAUSE:** Determines which interrupt(s) occurred
3. **6502 acknowledges:** Writes to IRQ_CAUSE_LOW/HIGH to clear specific interrupts (write-1-to-clear)
4. **MIA clears interrupts:** Clears specified bits in IRQ_CAUSE and deasserts IRQ line if no enabled interrupts remain
5. **New interrupts can occur:** Same interrupt type can trigger again

**Write-1-to-Clear Mechanism:**
- Writing a 1 to a bit position in $C0F1 or $C0F2 clears that interrupt bit
- Writing a 0 has no effect (bit remains unchanged)
- Allows selective acknowledgment of specific interrupts
- Multiple interrupts can be cleared in a single write

**Multiple Pending Interrupts:**
- Multiple interrupt bits can be set simultaneously in IRQ_CAUSE
- IRQ line remains asserted until all enabled pending interrupts are cleared
- 6502 can acknowledge interrupts individually or in groups

**Example 1: Selective Acknowledgment**
```assembly
; Read which interrupts are pending
LDA $C0F1       ; Read IRQ_CAUSE_LOW = 0x15 (bits 0, 2, 4 set)

; Handle memory error (bit 0)
; ...

; Acknowledge only memory error interrupt
LDA #$01        ; Write 1 to bit 0
STA $C0F1       ; Clears bit 0, bits 2 and 4 remain pending

; Later, acknowledge remaining interrupts
LDA #$14        ; Write 1s to bits 2 and 4
STA $C0F1       ; Clears bits 2 and 4
```

**Example 2: Acknowledge All at Once**
```assembly
; Read pending interrupts
LDA $C0F1       ; Read IRQ_CAUSE_LOW
PHA             ; Save for later

; Handle all interrupts
; ...

; Acknowledge all low byte interrupts
PLA             ; Restore IRQ_CAUSE_LOW value
STA $C0F1       ; Clear all bits that were set

; Or use CMD_CLEAR_IRQ to clear all interrupts (both low and high)
LDA #$05        ; CMD_CLEAR_IRQ
STA $C004       ; Clears all pending interrupts (any window)
```

**Example 3: Video Interrupt**
```assembly
; Read video interrupts
LDA $C0F2       ; Read IRQ_CAUSE_HIGH

; Check for video frame complete (bit 0 of high byte)
AND #$01
BEQ NO_FRAME

; Handle frame complete
; ...

; Acknowledge video frame interrupt
LDA #$01        ; Write 1 to bit 0 of high byte
STA $C0F2       ; Clears video frame interrupt

NO_FRAME:
```

## Wrap-on-Limit Feature

The wrap-on-limit feature allows an index to automatically reset to its default address when it reaches a specified limit address. This is useful for:
- Circular buffers with custom sizes
- Bounded iteration over memory regions
- Tile/sprite processing with automatic wraparound

**How it works:**
1. Set the limit address using CFG_LIMIT_L/M/H fields
2. Enable FLAG_WRAP_ON_LIMIT (bit 2) in the FLAGS register
3. When auto-stepping causes the address to reach or exceed the limit, it wraps to the default address

**Example: 64-byte circular buffer**
```assembly
; Configure index for 64-byte circular buffer at $20000
LDA #64         ; Select index
STA $C000       ; Window A

; Set default address (buffer start)
LDA #$03        ; CFG_DEFAULT_L
STA $C002
LDA #$00
STA $C003       ; Default = $20000 (low byte)

; Set limit address (buffer end + 1)
LDA #$06        ; CFG_LIMIT_L
STA $C002
LDA #$40        ; 64 bytes = $40
STA $C003       ; Limit = $20040

; Enable auto-step and wrap-on-limit
LDA #$0A        ; CFG_FLAGS
STA $C002
LDA #$05        ; AUTO_STEP | WRAP_ON_LIMIT
STA $C003

; Now reading/writing will automatically wrap at 64 bytes
```

## Usage Examples

### Basic Index Access
```assembly
; Select index 16 (Character Table 0) in Window A
LDA #16
STA $C000

; Reset index to its default position
LDA #$01
STA $C004

; Write character data
LDA #$FF        ; Character pixel data
STA $C001       ; Write to DATA_PORT (auto-increments)
LDA #$00
STA $C001       ; Next byte
; ... continue writing character data
```

### Multi-Window Copy Operation
```assembly
; Set up source in Window A (index 16 - Character Table 0)
LDA #16
STA $C000       ; Select index 16 in Window A

; Set up destination in Window B (index 17 - Character Table 1)  
LDA #17
STA $C010       ; Select index 17 in Window B

; Copy data byte by byte
COPY_LOOP:
    LDA $C001   ; Read from Window A (index 16)
    STA $C011   ; Write to Window B (index 17)
    ; Check if more data to copy...
    BNE COPY_LOOP
```

### Four-Window Parallel Access
```assembly
; Set up four different indexes in four windows
LDA #16
STA $C000       ; Window A: Character Table 0

LDA #32
STA $C010       ; Window B: Palette Bank 0

LDA #48
STA $C020       ; Window C: Nametable 0

LDA #56
STA $C030       ; Window D: Sprite OAM

; Now can access all four data sources simultaneously
LDA $C001       ; Read from Character Table 0
STA $C021       ; Write to Nametable 0
LDA $C011       ; Read from Palette Bank 0
STA $C031       ; Write to Sprite OAM
```

### DMA Block Copy
```assembly
; Set up DMA copy from index 16 to index 17, 256 bytes
LDA #16
STA $C000       ; Select any index for configuration

LDA #$0B        ; Select COPY_SRC_IDX field
STA $C002
LDA #16         ; Source index
STA $C003

LDA #$0C        ; Select COPY_DST_IDX field  
STA $C002
LDA #17         ; Destination index
STA $C003

LDA #$0D        ; Select COPY_COUNT_L field
STA $C002
LDA #$00        ; Low byte of count (256 = $0100)
STA $C003

LDA #$0E        ; Select COPY_COUNT_H field
STA $C002  
LDA #$01        ; High byte of count
STA $C003

; Execute block copy
LDA #$07        ; COPY_BLOCK command
STA $C004       ; Can use any window

; Wait for completion
WAIT_DMA:
    LDA $C0F0   ; Read DEVICE_STATUS (shared register)
    AND #$40    ; Check DMA_ACTIVE bit
    BNE WAIT_DMA
```

### Interrupt Handling
```assembly
; Enable specific interrupts
LDA #$11        ; Enable MEMORY_ERROR, USB_KEYBOARD (bits 0 and 4)
STA $C0F3       ; IRQ_MASK_LOW

LDA #$03        ; Enable VIDEO_FRAME, VIDEO_COLLISION
STA $C0F4       ; IRQ_MASK_HIGH

LDA #$01        ; Enable global interrupts
STA $C0F5       ; IRQ_ENABLE

; In IRQ handler:
IRQ_HANDLER:
    ; Save registers
    PHA
    TXA
    PHA
    TYA
    PHA
    
    ; Check interrupt source
    LDA $C0F1   ; Read IRQ_CAUSE_LOW
    BEQ CHECK_HIGH
    
    ; Handle low byte interrupts
    AND #$01
    BEQ NOT_MEM_ERR
    ; Handle memory error
    LDA #$01
    STA $C0F1   ; Clear memory error interrupt
NOT_MEM_ERR:
    
CHECK_HIGH:
    LDA $C0F2   ; Read IRQ_CAUSE_HIGH
    BEQ IRQ_DONE
    
    ; Handle high byte interrupts
    AND #$01
    BEQ IRQ_DONE
    ; Handle video frame complete
    LDA #$01
    STA $C0F2   ; Clear video frame interrupt
    
IRQ_DONE:
    ; Restore registers
    PLA
    TAY
    PLA
    TAX
    PLA
    RTI
```

## Address Mirroring Examples

Due to MIA only seeing 8 address lines, the 256-byte register space mirrors throughout the 1KB range:

```assembly
; These all access the same register (Window A, IDX_SELECT):
LDA $C000       ; Primary address
LDA $C100       ; Mirror 1
LDA $C200       ; Mirror 2
LDA $C300       ; Mirror 3

; These all access the same register (Window B, DATA_PORT):
LDA $C011       ; Primary address
LDA $C111       ; Mirror 1
LDA $C211       ; Mirror 2
LDA $C311       ; Mirror 3

; These all access the same register (Shared, DEVICE_STATUS):
LDA $C0F0       ; Primary address
LDA $C1F0       ; Mirror 1
LDA $C2F0       ; Mirror 2
LDA $C3F0       ; Mirror 3
```

This mirroring is automatic and transparent to software. Use whichever address is most convenient for your code.
