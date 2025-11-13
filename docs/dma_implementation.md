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

### 1. Asynchronous Operation

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

### 3. Interrupt-Driven Completion

DMA completion is signaled via interrupt:
- Hardware triggers interrupt when transfer completes
- Callback clears STATUS_DMA_ACTIVE flag
- Sets IRQ_DMA_COMPLETE for 6502 to detect

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

### `indexed_memory_dma_is_busy()`

Check if a transfer is currently in progress.

**Returns:** `true` if busy, `false` if idle

### `indexed_memory_dma_wait_for_completion()`

Block until current transfer completes.

**Use case:** When starting a new transfer while one is active

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

## Bus Timing Considerations

From `docs/bus_timing.md`, the 6502 at 1 MHz has:
- 1000ns cycle time
- 785ns available for read response

With DMA:
- Copy operations don't block bus interface
- Core 0 remains responsive to 6502 requests
- Large transfers (video frames, etc.) can happen in background

## Usage Example

```c
// Setup indexes for video buffer copy
indexed_memory_set_index_address(src_idx, VIDEO_BUFFER_A);
indexed_memory_set_index_address(dst_idx, VIDEO_BUFFER_B);

// Start DMA transfer (returns immediately)
indexed_memory_copy_block(src_idx, dst_idx, 4000);  // 40x25 screen

// IMPORTANT: Indexes are NOT modified by copy operations
// After copy, src_idx still points to VIDEO_BUFFER_A
// and dst_idx still points to VIDEO_BUFFER_B

// Core continues servicing 6502 bus
// DMA runs in parallel

// 6502 can poll STATUS_DMA_ACTIVE or wait for IRQ_DMA_COMPLETE
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
