# MIA Indexed Memory Interface Reference

## 6502 Address Map (Dual Window Interface)

| Address | Window A | Window B | Access | Function |
|---------|----------|----------|--------|----------|
| $C000 | IDX_SELECT_A | IDX_SELECT_B | R/W | Selects active index (0-255) for each window |
| $C001 | DATA_PORT_A | DATA_PORT_B | R/W | Read/write byte at current index address (auto-steps) |
| $C002 | CFG_FIELD_SELECT_A | CFG_FIELD_SELECT_B | R/W | Selects configuration field for active index |
| $C003 | CFG_DATA_A | CFG_DATA_B | R/W | Read/write selected configuration field |
| $C004 | COMMAND_A | COMMAND_B | W | Issues control commands |
| $C005 | RESERVED_A | RESERVED_B | - | Reserved for future use |
| $C006 | STATUS | STATUS | R | Device status bits (shared) |
| $C007 | IRQ_CAUSE | IRQ_CAUSE | R | Interrupt source identification (shared) |

**Address Range and Mirroring:**
- **Active Range:** $C000-$C3FF (1KB total address space)
- **Register Space:** 16 bytes ($C000-$C00F) mirrored throughout the 1KB range
- **Window A:** $C000-$C007 (Primary window, higher priority)
- **Window B:** $C008-$C00F (Secondary window, ignored when conflicts occur)

**Control Signals:**
- **IO0_CS:** GPIO 21 (active low) - Chip select for indexed memory interface
- **WE:** GPIO 18 (active low) - Write Enable
- **OE:** GPIO 19 (active low) - Output Enable
- **IRQ:** GPIO 26 - Interrupt line to 6502 CPU

**Shared Resources:**
- Both windows access the same pool of 256 indexes (0-255)
- Each window maintains independent active index selection
- Window A has priority in case of simultaneous access conflicts

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
| 128-255 | User/Application | Available for user applications and general RAM (162KB) |

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
| 0x06 | STEP | 8-bit | Step size (0-255 bytes) for auto-increment |
| 0x07 | FLAGS | 8-bit | Index behavior control flags |

### DMA Configuration Fields
| Field ID | Name | Width | Description |
|----------|------|-------|-------------|
| 0x08 | COPY_SRC_IDX | 8-bit | Source index for copy operations |
| 0x09 | COPY_DST_IDX | 8-bit | Destination index for copy operations |
| 0x0A | COPY_COUNT_L | 8-bit | Low byte of copy byte count |
| 0x0B | COPY_COUNT_H | 8-bit | High byte of copy byte count (16-bit total) |
| 0x0C-0x0F | RESERVED | 8-bit | Reserved for future expansion |

## FLAGS Register Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | AUTO_STEP | 0=Manual stepping, 1=Auto-step after DATA_PORT access |
| 1 | DIRECTION | 0=Forward/increment, 1=Backward/decrement |
| 2-7 | RESERVED | Reserved for future use |

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

## STATUS Register Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | BUSY | Command in progress |
| 1 | IRQ_PENDING | Interrupt pending |
| 2 | MEMORY_ERROR | Invalid memory access occurred |
| 3 | INDEX_OVERFLOW | Address pointer overflow/underflow |
| 4 | USB_DATA_READY | USB keyboard data available |
| 5 | VIDEO_FRAME_READY | Video frame ready for transmission |
| 6 | DMA_ACTIVE | DMA/copy operation in progress |
| 7 | SYSTEM_READY | MIA fully initialized and ready |

## IRQ_CAUSE Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | NO_IRQ | No interrupt pending |
| 0x01 | MEMORY_ERROR | Invalid memory access |
| 0x02 | INDEX_OVERFLOW | Address pointer overflow |
| 0x03 | DMA_COMPLETE | DMA/copy operation completed |
| 0x04 | DMA_ERROR | DMA/copy operation failed |
| 0x10 | USB_KEYBOARD | Keyboard data received |
| 0x11 | USB_DEVICE_CHANGE | USB device connected/disconnected |
| 0x20 | VIDEO_FRAME_COMPLETE | Video frame transmission complete |
| 0x21 | VIDEO_COLLISION | Sprite collision detected |
| 0x30 | SYSTEM_ERROR | General system error |

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

### Dual Window Copy Operation
```assembly
; Set up source in Window A (index 16 - Character Table 0)
LDA #16
STA $C000       ; Select index 16 in Window A

; Set up destination in Window B (index 17 - Character Table 1)  
LDA #17
STA $C008       ; Select index 17 in Window B

; Copy data byte by byte
COPY_LOOP:
    LDA $C001   ; Read from Window A (index 16)
    STA $C009   ; Write to Window B (index 17)
    ; Check if more data to copy...
    BNE COPY_LOOP
```

### DMA Block Copy
```assembly
; Set up DMA copy from index 16 to index 17, 256 bytes
LDA #16
STA $C000       ; Select any index for configuration

LDA #$08        ; Select COPY_SRC_IDX field
STA $C002
LDA #16         ; Source index
STA $C003

LDA #$09        ; Select COPY_DST_IDX field  
STA $C002
LDA #17         ; Destination index
STA $C003

LDA #$0A        ; Select COPY_COUNT_L field
STA $C002
LDA #$00        ; Low byte of count (256 = $0100)
STA $C003

LDA #$0B        ; Select COPY_COUNT_H field
STA $C002  
LDA #$01        ; High byte of count
STA $C003

; Execute block copy
LDA #$07        ; COPY_BLOCK command
STA $C004

; Wait for completion
WAIT_DMA:
    LDA $C006   ; Read STATUS
    AND #$40    ; Check DMA_ACTIVE bit
    BNE WAIT_DMA
```