# DMA Implementation for Indexed Memory

## Overview

The indexed memory system uses hardware DMA for efficient bulk memory copy operations. This allows large data transfers to happen in the background without blocking the CPU, which is critical for maintaining 6502 bus timing requirements.

## Architecture

The DMA implementation uses a clean abstraction layer that separates hardware-specific code from the core logic:

```
indexed_memory.c
    ↓ (uses)
indexed_memory_dma.h (abstraction interface)
    ↓ (implemented by)
    ├─ indexed_memory_dma_hw.c (hardware implementation for Pico)
    └─ indexed_memory_dma_mock.c (mock implementation for tests)
```

## Key Features

### 1. Non-Blocking Operation

**Critical for bus timing:** DMA operations NEVER block the CPU, even if DMA is already busy.

```c
// If DMA is already running, new transfers are rejected immediately
indexed_memory_copy_block(src, dst, 1000);  // Returns immediately
// Either starts transfer OR triggers IRQ_DMA_ERROR
// NEVER blocks waiting
```

**Why this matters:**
- 6502 bus requires response within ~785ns at 1 MHz
- Blocking would cause bus timing violations
- MIA must remain responsive at all times

### 2. Asynchronous Operation

Block copy operations return immediately and execute in the background:

```c
// Start copy (returns immediately)
indexed_memory_copy_block(src_idx, dst_idx, 1024);

// 6502 can continue accessing other registers
// DMA runs in parallel on dedicated hardware

// IRQ_DMA_COMPLETE fires when done
```

### 2. Zero CPU Overhead

Once configured, DMA transfers require no CPU intervention:
- No polling loops
- No byte-by-byte copying
- CPU is free for bus interface, USB, video, etc.

### 3. Interrupt-Driven Completion and Errors

DMA operations signal completion and errors via interrupts:

**On Success:**
- Hardware triggers interrupt when transfer completes
- Callback clears STATUS_DMA_ACTIVE flag
- Sets IRQ_DMA_COMPLETE for 6502 to detect

**On Error (DMA Busy):**
- If DMA is already active, new transfer is rejected
- IRQ_DMA_ERROR is triggered immediately
- STATUS_DMA_ACTIVE remains set (original transfer still running)
- 6502 should wait for IRQ_DMA_COMPLETE before retrying

### 4. Testable Design

Mock implementation allows full unit testing:
- Same interface as hardware version
- Synchronous operation for predictable tests
- No hardware dependencies

## Design Decisions

### Index Behavior During Copy Operations

**Important:** Copy operations (both `indexed_memory_copy_byte` and `indexed_memory_copy_block`) do NOT modify the source or destination indexes.

**Rationale:**
- Indexes serve as **address pointers** for the copy operation
- They specify WHERE to copy from/to, not HOW to iterate
- Auto-step flags are ignored during copy operations
- This keeps copy semantics simple and predictable

**Example:**
```c
// Set up indexes
indexed_memory_set_index_address(10, 0x1000);  // Source
indexed_memory_set_index_address(11, 0x2000);  // Destination

// Copy 100 bytes
indexed_memory_copy_block(10, 11, 100);

// After copy:
// - Index 10 still points to 0x1000 (unchanged)
// - Index 11 still points to 0x2000 (unchanged)
// - Memory at 0x2000-0x2063 now contains copy of 0x1000-0x1063
```

**Contrast with indexed_memory_read/write:**
- `indexed_memory_read(idx)` DOES respect auto-step and modifies the index
- `indexed_memory_write(idx, data)` DOES respect auto-step and modifies the index
- Copy operations are bulk transfers and do NOT modify indexes

## API Reference

### `indexed_memory_dma_init()`

Initialize DMA subsystem. Called once during system startup.

**Returns:** DMA channel number (or 0 for mock)

### `indexed_memory_dma_start_transfer(dst, src, count)`

Start an asynchronous memory transfer.

**Parameters:**
- `dst`: Destination address
- `src`: Source address  
- `count`: Number of bytes to transfer

**Behavior:**
- Hardware: Returns immediately, transfer continues in background
- Mock: Completes synchronously, then returns

**Important:** If a DMA transfer is already in progress, the new transfer will be **rejected** and `IRQ_DMA_ERROR` will be triggered. The function returns immediately without blocking.

### `indexed_memory_dma_is_busy()`

Check if a transfer is currently in progress.

**Returns:** `true` if busy, `false` if idle

### `indexed_memory_dma_wait_for_completion()`

Block until current transfer completes.

**⚠️ WARNING:** This function blocks and should NOT be used during normal operation as it violates 6502 bus timing requirements.

**Valid use cases:**
- System shutdown/cleanup (when bus timing no longer matters)
- Debugging/testing (when running without 6502 attached)
- Initialization sequences (before bus interface is active)

**Do NOT use for:**
- Starting new transfers (use error handling instead)
- Any operation that could be called during normal 6502 bus activity

### `indexed_memory_dma_set_completion_callback(callback)`

Set function to call when transfer completes.

**Callback context:** Interrupt handler (keep it fast!)

## Performance Characteristics

### Single Byte Copy

Uses direct memory access (not DMA):
- DMA setup overhead > actual copy time
- ~10-20 CPU cycles
- Synchronous operation

### Block Copy (DMA)

Uses hardware DMA:
- Setup: ~50-100 CPU cycles
- Transfer: 0 CPU cycles (runs in parallel)
- Throughput: ~125 MB/s (at 125 MHz system clock)
- Asynchronous operation

### Example Timing

Copying 1KB of video data:

**Without DMA (old approach):**
- 1024 iterations × ~50 cycles = 51,200 cycles
- At 133 MHz: ~385 microseconds
- **Blocks 6502 bus for 385 µs** ❌

**With DMA (new approach):**
- Setup: ~100 cycles
- Transfer: 0 CPU cycles (parallel)
- At 133 MHz: ~0.75 microseconds setup time
- **6502 bus available after 0.75 µs** ✅

## Error Handling

### DMA Busy Error

When a transfer is requested while DMA is already active:

**What Happens:**
1. Function returns immediately (does NOT block)
2. `IRQ_DMA_ERROR` interrupt is triggered
3. `STATUS_DMA_ACTIVE` remains set (original transfer continues)
4. New transfer is NOT started

**6502 Response Pattern:**

```assembly
; Method 1: Poll before starting transfer
.check_dma:
    LDA STATUS_REG
    AND #STATUS_DMA_ACTIVE
    BNE .check_dma          ; Wait until idle
    
    ; Now safe to start transfer
    LDA #CMD_COPY_BLOCK
    STA COMMAND_REG

; Method 2: Use interrupts
.start_transfer:
    LDA #CMD_COPY_BLOCK
    STA COMMAND_REG
    ; If DMA busy, IRQ_DMA_ERROR will fire
    ; If successful, IRQ_DMA_COMPLETE will fire when done
    
.irq_handler:
    LDA IRQ_CAUSE_LOW
    AND #IRQ_DMA_ERROR
    BNE .dma_was_busy       ; Transfer rejected
    
    AND #IRQ_DMA_COMPLETE
    BNE .dma_finished       ; Transfer completed
```

## Bus Timing Considerations

From `docs/bus_timing.md`, the 6502 at 1 MHz has:
- 1000ns cycle time
- 785ns available for read response

With DMA:
- Copy operations don't block bus interface (even on error)
- Core 0 remains responsive to 6502 requests at all times
- Large transfers (video frames, etc.) can happen in background
- Rejected transfers return immediately without blocking

## Usage Example

```c
// Setup indexes for video buffer copy
indexed_memory_set_index_address(src_idx, VIDEO_BUFFER_A);
indexed_memory_set_index_address(dst_idx, VIDEO_BUFFER_B);

// Check if DMA is idle before starting transfer
if (!(indexed_memory_get_status() & STATUS_DMA_ACTIVE)) {
    // Start DMA transfer (returns immediately)
    indexed_memory_copy_block(src_idx, dst_idx, 4000);  // 40x25 screen
    
    // IMPORTANT: Indexes are NOT modified by copy operations
    // After copy, src_idx still points to VIDEO_BUFFER_A
    // and dst_idx still points to VIDEO_BUFFER_B
    
    // Core continues servicing 6502 bus
    // DMA runs in parallel
    
    // 6502 can poll STATUS_DMA_ACTIVE or wait for IRQ_DMA_COMPLETE
} else {
    // DMA busy - transfer rejected
    // IRQ_DMA_ERROR will be triggered
    // Wait for IRQ_DMA_COMPLETE before retrying
}
```

## Testing

The mock implementation allows full testing without hardware:

```c
// Test behaves identically to hardware version
indexed_memory_copy_block(src_idx, dst_idx, 100);

// Mock completes synchronously
assert(g_state.status & STATUS_DMA_ACTIVE == 0);
assert(irq_cause & IRQ_DMA_COMPLETE);
```

## Future Enhancements

Possible improvements:
1. **Multiple DMA channels** - Parallel transfers
2. **Scatter-gather** - Non-contiguous memory regions
3. **2D transfers** - For video operations
4. **Priority levels** - Critical vs. background transfers

## References

- RP2040 Datasheet: Section 2.5 (DMA)
- `src/indexed_memory/indexed_memory_dma.h` - API definition
- `src/indexed_memory/indexed_memory_dma_hw.c` - Hardware implementation
- `tests/mocks/indexed_memory_dma_mock.c` - Test mock
