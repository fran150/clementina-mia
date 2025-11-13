# MIA Programming Guide

Complete guide for programming the MIA (Multifunction Interface Adapter) from 6502 assembly code.

## Table of Contents

1. [Overview](#overview)
2. [Indexed Memory System](#indexed-memory-system)
3. [DMA Copy Operations](#dma-copy-operations)
4. [Error Handling](#error-handling)
5. [Interrupt System](#interrupt-system)
6. [Usage Examples](#usage-examples)

---

## Overview

The MIA provides a powerful indexed memory system that allows the 6502 to access 256KB of memory through 256 independent indexes. Each index acts as a pointer with automatic stepping, wrapping, and DMA capabilities.

### Key Features

- **256 memory indexes** - Independent pointers into 256KB address space
- **Hardware DMA** - Background memory copies without CPU overhead
- **Auto-stepping** - Automatic pointer advancement for sequential access
- **Interrupt-driven** - Completion and error notifications via IRQ
- **Non-blocking** - Never blocks the 6502, maintains bus timing

---

## Indexed Memory System

### Basic Concept

Each index is a 24-bit pointer into MIA's 256KB memory space. You can:
- Read/write data through an index
- Configure index behavior (step size, direction, wrapping)
- Use indexes for DMA operations

### Accessing Data

```assembly
; Select an index (e.g., index 10)
LDA #10
STA IDX_SELECT

; Read data from current address
LDA DATA_PORT          ; Read byte, index auto-steps if enabled

; Write data to current address
LDA #$42
STA DATA_PORT          ; Write byte, index auto-steps if enabled
```

### Configuring Indexes

Indexes have several configuration fields:

| Field | ID | Description |
|-------|-----|-------------|
| ADDR_L | 0x00 | Address low byte |
| ADDR_M | 0x01 | Address middle byte |
| ADDR_H | 0x02 | Address high byte (24-bit) |
| DEFAULT_L/M/H | 0x03-0x05 | Default address for reset |
| LIMIT_L/M/H | 0x06-0x08 | Wrap limit address |
| STEP | 0x09 | Step size (1-255) |
| FLAGS | 0x0A | Behavior flags |

**Example: Configure index for sequential access**

```assembly
; Select index 10
LDA #10
STA IDX_SELECT

; Select ADDR_L field
LDA #CFG_ADDR_L
STA CFG_FIELD_SELECT

; Set address to $1000
LDA #$00
STA CFG_DATA           ; Low byte

LDA #CFG_ADDR_M
STA CFG_FIELD_SELECT
LDA #$10
STA CFG_DATA           ; Middle byte

LDA #CFG_ADDR_H
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA           ; High byte

; Set step size to 1
LDA #CFG_STEP
STA CFG_FIELD_SELECT
LDA #1
STA CFG_DATA

; Enable auto-step
LDA #CFG_FLAGS
STA CFG_FIELD_SELECT
LDA #FLAG_AUTO_STEP
STA CFG_DATA

; Now reads/writes will auto-increment
LDA #10
STA IDX_SELECT
LDA DATA_PORT          ; Read from $1000, index becomes $1001
LDA DATA_PORT          ; Read from $1001, index becomes $1002
```

### Configuration Flags

| Flag | Bit | Description |
|------|-----|-------------|
| FLAG_AUTO_STEP | 0x01 | Automatically step after read/write |
| FLAG_DIRECTION | 0x02 | 0=forward, 1=backward |
| FLAG_WRAP_ON_LIMIT | 0x04 | Wrap to default when reaching limit |

---

## DMA Copy Operations

DMA allows copying large blocks of memory without CPU intervention. The copy happens in the background using hardware DMA.

### How DMA Works

1. **Configure source and destination indexes**
2. **Set copy count**
3. **Execute COPY_BLOCK command**
4. **Function returns immediately** - DMA runs in background
5. **IRQ_DMA_COMPLETE fires when done**

### Important: Non-Blocking Behavior

**DMA operations NEVER block the CPU.** If DMA is already busy:
- New transfer is rejected immediately
- `IRQ_DMA_ERROR` interrupt fires
- `STATUS_DMA_ACTIVE` remains set
- You must wait for `IRQ_DMA_COMPLETE` before retrying

This is critical for maintaining 6502 bus timing (785ns response requirement at 1 MHz).

### Configuring DMA

```assembly
; Set source index
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx
STA CFG_DATA

; Set destination index
LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #dst_idx
STA CFG_DATA

; Set count (16-bit)
LDA #CFG_COPY_COUNT_L
STA CFG_FIELD_SELECT
LDA #<count
STA CFG_DATA

LDA #CFG_COPY_COUNT_H
STA CFG_FIELD_SELECT
LDA #>count
STA CFG_DATA

; Execute copy
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
; Returns immediately, DMA runs in background
```

### Index Behavior During Copy

**Important:** Copy operations do NOT modify indexes. They use indexes as address pointers only.

```assembly
; Before copy
; Index 10 points to $1000
; Index 11 points to $2000

; Copy 100 bytes
; (configure DMA as shown above)
LDA #CMD_COPY_BLOCK
STA COMMAND_REG

; After copy
; Index 10 STILL points to $1000 (unchanged)
; Index 11 STILL points to $2000 (unchanged)
; Memory at $2000-$2063 now contains copy of $1000-$1063
```

This is different from read/write operations, which DO respect auto-step and modify indexes.

---

## Error Handling

### DMA Busy Error

When you try to start a DMA transfer while one is already running:

**What happens:**
1. Transfer is rejected immediately (does NOT block)
2. `IRQ_DMA_ERROR` interrupt fires
3. `STATUS_DMA_ACTIVE` remains set
4. Original transfer continues

**How to handle:**

**Method 1: Poll before transfer**
```assembly
.wait_dma:
    LDA STATUS_REG
    AND #STATUS_DMA_ACTIVE
    BNE .wait_dma          ; Wait until idle
    
    ; Now safe to start transfer
    LDA #CMD_COPY_BLOCK
    STA COMMAND_REG
```

**Method 2: Use interrupts**
```assembly
; Just try to start transfer
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
; If busy: IRQ_DMA_ERROR fires
; If successful: IRQ_DMA_COMPLETE fires when done

; In IRQ handler:
.irq_handler:
    LDA IRQ_CAUSE_LOW
    AND #IRQ_DMA_ERROR
    BNE .dma_busy          ; Retry later
    
    AND #IRQ_DMA_COMPLETE
    BNE .dma_done          ; Process completion
```

### Memory Errors

If you access invalid memory addresses:
- `STATUS_MEMORY_ERROR` flag is set
- `IRQ_MEMORY_ERROR` interrupt fires
- Read returns $00
- Write is ignored

---

## Interrupt System

### IRQ Causes

**Low byte (bits 0-7):**
| Bit | Name | Description |
|-----|------|-------------|
| 0 | IRQ_MEMORY_ERROR | Invalid memory access |
| 1 | IRQ_INDEX_OVERFLOW | Index exceeded limit |
| 2 | IRQ_DMA_COMPLETE | DMA transfer finished |
| 3 | IRQ_DMA_ERROR | DMA busy (transfer rejected) |
| 4 | IRQ_USB_KEYBOARD | Keyboard data available |
| 5 | IRQ_USB_DEVICE_CHANGE | USB device connected/disconnected |

**High byte (bits 8-15):**
| Bit | Name | Description |
|-----|------|-------------|
| 8 | IRQ_VIDEO_FRAME_COMPLETE | Video frame transmitted |
| 9 | IRQ_VIDEO_COLLISION | Sprite collision detected |

### Checking Interrupts

```assembly
irq_handler:
    ; Read low byte
    LDA IRQ_CAUSE_LOW
    
    ; Check for DMA completion
    AND #IRQ_DMA_COMPLETE
    BNE .handle_dma_complete
    
    ; Check for DMA error
    LDA IRQ_CAUSE_LOW
    AND #IRQ_DMA_ERROR
    BNE .handle_dma_error
    
    ; ... check other interrupts ...
    RTI

.handle_dma_complete:
    ; Process DMA completion
    ; Clear interrupt by writing 1 to the bit
    LDA #IRQ_DMA_COMPLETE
    STA IRQ_CAUSE_LOW
    RTI

.handle_dma_error:
    ; Handle DMA busy error
    ; Clear interrupt
    LDA #IRQ_DMA_ERROR
    STA IRQ_CAUSE_LOW
    RTI
```

### Interrupt Masking

You can enable/disable specific interrupts:

```assembly
; Disable DMA error interrupts
LDA IRQ_MASK_LOW
AND #~IRQ_DMA_ERROR
STA IRQ_MASK_LOW

; Enable DMA completion interrupts
LDA IRQ_MASK_LOW
ORA #IRQ_DMA_COMPLETE
STA IRQ_MASK_LOW
```

---

## Usage Examples

### Example 1: Copy Video Buffer

```assembly
; Copy 4000 bytes from front buffer to back buffer
; (40x25 screen = 1000 bytes, 4 bytes per cell = 4000 bytes)

; Set up source index (front buffer at $10000)
LDA #10
STA IDX_SELECT

LDA #CFG_ADDR_L
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_M
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_H
STA CFG_FIELD_SELECT
LDA #$01
STA CFG_DATA

; Set up destination index (back buffer at $20000)
LDA #11
STA IDX_SELECT

LDA #CFG_ADDR_L
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_M
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_H
STA CFG_FIELD_SELECT
LDA #$02
STA CFG_DATA

; Configure DMA
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #10
STA CFG_DATA

LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #11
STA CFG_DATA

LDA #CFG_COPY_COUNT_L
STA CFG_FIELD_SELECT
LDA #<4000
STA CFG_DATA

LDA #CFG_COPY_COUNT_H
STA CFG_FIELD_SELECT
LDA #>4000
STA CFG_DATA

; Start copy
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
; Returns immediately, DMA runs in background
```

### Example 2: Sequential Data Processing

```assembly
; Read 256 bytes sequentially with auto-step

; Configure index 5 for sequential access
LDA #5
STA IDX_SELECT

; Set starting address
LDA #CFG_ADDR_L
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_M
STA CFG_FIELD_SELECT
LDA #$10
STA CFG_DATA

LDA #CFG_ADDR_H
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

; Enable auto-step
LDA #CFG_FLAGS
STA CFG_FIELD_SELECT
LDA #FLAG_AUTO_STEP
STA CFG_DATA

; Read 256 bytes
LDA #5
STA IDX_SELECT
LDX #0
.loop:
    LDA DATA_PORT          ; Read byte, index auto-increments
    STA buffer,X
    INX
    BNE .loop
```

### Example 3: Circular Buffer with Wrap

```assembly
; Configure index for circular buffer (64 bytes)

LDA #20
STA IDX_SELECT

; Set starting address ($5000)
LDA #CFG_ADDR_L
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_ADDR_M
STA CFG_FIELD_SELECT
LDA #$50
STA CFG_DATA

; Set default address (for wrap)
LDA #CFG_DEFAULT_L
STA CFG_FIELD_SELECT
LDA #$00
STA CFG_DATA

LDA #CFG_DEFAULT_M
STA CFG_FIELD_SELECT
LDA #$50
STA CFG_DATA

; Set limit address ($5040 = $5000 + 64)
LDA #CFG_LIMIT_L
STA CFG_FIELD_SELECT
LDA #$40
STA CFG_DATA

LDA #CFG_LIMIT_M
STA CFG_FIELD_SELECT
LDA #$50
STA CFG_DATA

; Enable auto-step and wrap-on-limit
LDA #CFG_FLAGS
STA CFG_FIELD_SELECT
LDA #(FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT)
STA CFG_DATA

; Now reads/writes will wrap at 64 bytes
```

---

## Performance Considerations

### DMA Performance

- **Setup overhead**: ~0.75µs
- **Transfer rate**: ~125 MB/s
- **CPU overhead during transfer**: 0% (runs in background)

**Example:** Copying 1KB takes ~8µs total (0.75µs setup + 8µs transfer), but CPU is only blocked for 0.75µs.

### Bus Timing

At 1 MHz 6502 clock:
- **Cycle time**: 1000ns
- **MIA response time**: <785ns (always met)
- **DMA never blocks**: Even on error, returns in ~150ns

---

## Best Practices

1. **Check DMA status before starting transfers** - Poll `STATUS_DMA_ACTIVE` or use interrupts
2. **Use interrupts for completion** - More efficient than polling
3. **Reuse index configuration** - Set up indexes once, use many times
4. **Group DMA operations** - Minimize setup overhead
5. **Handle errors gracefully** - Always check for `IRQ_DMA_ERROR`

---

## Quick Reference

### Register Addresses

| Register | Address | Description |
|----------|---------|-------------|
| IDX_SELECT | $C000 | Select active index (Window A) |
| DATA_PORT | $C001 | Read/write data through index |
| CFG_FIELD_SELECT | $C002 | Select configuration field |
| CFG_DATA | $C003 | Read/write configuration |
| COMMAND_REG | $C004 | Execute commands |
| STATUS_REG | $C0F0 | System status |
| IRQ_CAUSE_LOW | $C0F1 | IRQ cause low byte |
| IRQ_CAUSE_HIGH | $C0F2 | IRQ cause high byte |

### Common Commands

**Window-Level Commands** (via window COMMAND register at +0x04):
| Command | Code | Description |
|---------|------|-------------|
| CMD_RESET_INDEX | 0x01 | Reset active index to default address |
| CMD_SET_DEFAULT_TO_ADDR | 0x02 | Set default address to current address |
| CMD_SET_LIMIT_TO_ADDR | 0x03 | Set limit address to current address |

**Shared Commands** (via shared COMMAND register at $C0FF):
| Command | Code | Description |
|---------|------|-------------|
| CMD_RESET_ALL_IDX | 0x01 | Reset all 256 indexes to default addresses |
| CMD_FACTORY_RESET_ALL_IDX | 0x02 | Factory reset indexed memory subsystem |
| CMD_CLEAR_IRQ | 0x03 | Clear all interrupt flags |
| CMD_COPY_BLOCK | 0x04 | Start DMA copy operation |
| CMD_SYSTEM_RESET | 0x05 | Full hardware reset (reboots both MIA and 6502) |

### Status Flags

| Flag | Bit | Description |
|------|-----|-------------|
| STATUS_DMA_ACTIVE | 0x40 | DMA transfer in progress |
| STATUS_MEMORY_ERROR | 0x04 | Memory access error |
| STATUS_IRQ_PENDING | 0x02 | Interrupt pending |

---

For detailed timing specifications, see `bus_timing.md`.  
For complete API reference, see `mia_indexed_interface_reference.md`.
