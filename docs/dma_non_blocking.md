# DMA Non-Blocking Behavior

## Critical Design Decision

**DMA operations NEVER block**, even when DMA is already busy. This is essential for maintaining 6502 bus timing requirements.

## The Problem We Solved

### Original Implementation (WRONG)

```c
// Check if DMA is already active
if (g_state.status & STATUS_DMA_ACTIVE) {
    // Wait for previous transfer to complete
    indexed_memory_dma_wait_for_completion();  // ⚠️ BLOCKS!
}
```

**Why this was dangerous:**

1. **Blocks the CPU** - Could block for microseconds or milliseconds
2. **Violates bus timing** - 6502 expects response within ~785ns at 1 MHz
3. **Causes system hangs** - MIA becomes unresponsive to bus requests

### Example Failure Scenario

```
Time 0µs:    6502 starts DMA copy of 4KB video buffer
             (takes ~30µs to complete)

Time 10µs:   6502 tries to start another DMA copy
             MIA blocks in wait loop...

Time 10µs:   6502 tries to read STATUS register
             MIA is STILL blocked!
             Bus timing violated - system hangs ❌
```

## Current Implementation (CORRECT)

```c
// Check if DMA is already active
if (g_state.status & STATUS_DMA_ACTIVE) {
    // DMA already in progress - reject new transfer to avoid blocking
    // This prevents the MIA from missing 6502 bus timing requirements
    indexed_memory_set_irq(IRQ_DMA_ERROR);
    return;  // Return immediately, never block
}
```

**Benefits:**

1. **Never blocks** - Always returns immediately
2. **Maintains bus timing** - MIA remains responsive
3. **Clear error signaling** - 6502 gets IRQ_DMA_ERROR interrupt
4. **Predictable behavior** - 6502 can handle the error appropriately

## Behavior

### When DMA is Idle

```c
indexed_memory_copy_block(src_idx, dst_idx, 1000);
// ✅ Transfer starts
// ✅ STATUS_DMA_ACTIVE is set
// ✅ Function returns immediately
// ✅ IRQ_DMA_COMPLETE will fire when done
```

### When DMA is Busy

```c
// First transfer still running...
indexed_memory_copy_block(src_idx, dst_idx, 1000);
// ❌ Transfer is rejected
// ✅ STATUS_DMA_ACTIVE remains set (original transfer continues)
// ✅ IRQ_DMA_ERROR is triggered immediately
// ✅ Function returns immediately (does NOT block)
```

## 6502 Usage Patterns

### Pattern 1: Poll Before Transfer

```assembly
; Check if DMA is idle before starting
.wait_dma_idle:
    LDA STATUS_REG
    AND #STATUS_DMA_ACTIVE
    BNE .wait_dma_idle      ; Loop until idle
    
    ; Now safe to start transfer
    LDA #src_idx
    STA CFG_COPY_SRC
    LDA #dst_idx
    STA CFG_COPY_DST
    LDA #<count
    STA CFG_COPY_COUNT_L
    LDA #>count
    STA CFG_COPY_COUNT_H
    LDA #CMD_COPY_BLOCK
    STA COMMAND_REG
```

### Pattern 2: Interrupt-Driven

```assembly
; Start transfer without checking
start_transfer:
    LDA #CMD_COPY_BLOCK
    STA COMMAND_REG
    ; If busy: IRQ_DMA_ERROR fires
    ; If successful: IRQ_DMA_COMPLETE fires when done
    RTS

; IRQ handler
irq_handler:
    ; Check IRQ cause
    LDA IRQ_CAUSE_LOW
    
    ; Check for DMA error (busy)
    AND #IRQ_DMA_ERROR
    BNE .dma_was_busy
    
    ; Check for DMA completion
    LDA IRQ_CAUSE_LOW
    AND #IRQ_DMA_COMPLETE
    BNE .dma_finished
    
    RTI

.dma_was_busy:
    ; Transfer was rejected because DMA was busy
    ; Wait for IRQ_DMA_COMPLETE, then retry
    ; Clear IRQ and return
    LDA #IRQ_DMA_ERROR
    STA IRQ_CAUSE_LOW       ; Write 1 to clear
    RTI

.dma_finished:
    ; Transfer completed successfully
    ; Process completion
    LDA #IRQ_DMA_COMPLETE
    STA IRQ_CAUSE_LOW       ; Write 1 to clear
    RTI
```

### Pattern 3: Queue Transfers (Software)

```assembly
; Software queue for pending transfers
transfer_queue:
    .byte 0, 0, 0, 0  ; src_idx, dst_idx, count_l, count_h

; Try to start transfer, queue if busy
queue_transfer:
    ; Check if DMA is busy
    LDA STATUS_REG
    AND #STATUS_DMA_ACTIVE
    BNE .queue_it           ; Busy, queue for later
    
    ; Start immediately
    JSR start_transfer
    RTS

.queue_it:
    ; Save transfer parameters
    LDA src_idx
    STA transfer_queue+0
    LDA dst_idx
    STA transfer_queue+1
    LDA count_l
    STA transfer_queue+2
    LDA count_h
    STA transfer_queue+3
    RTS

; In IRQ handler, after DMA_COMPLETE:
.check_queue:
    ; Check if there's a queued transfer
    LDA transfer_queue+0
    BEQ .no_queue           ; No queued transfer
    
    ; Start queued transfer
    LDA transfer_queue+0
    STA CFG_COPY_SRC
    LDA transfer_queue+1
    STA CFG_COPY_DST
    ; ... etc ...
    
    ; Clear queue
    LDA #0
    STA transfer_queue+0
```

## Bus Timing Analysis

### At 1 MHz 6502 Clock

- **Cycle time**: 1000ns
- **Available response time**: ~785ns
- **DMA transfer time** (1KB): ~30µs

**Without non-blocking:**
- If DMA blocks for 30µs, that's **30 missed bus cycles**
- System would hang or crash

**With non-blocking:**
- Error return takes ~50ns
- Well within 785ns budget
- System remains stable ✅

## Testing

The non-blocking behavior is explicitly tested:

```c
// Simulate DMA busy
g_state.status |= STATUS_DMA_ACTIVE;

// Try to start another transfer
indexed_memory_copy_block(src_idx, dst_idx, 5);

// Verify error IRQ was triggered
assert(indexed_memory_get_irq_cause() & IRQ_DMA_ERROR);

// Verify DMA still marked as active
assert(indexed_memory_get_status() & STATUS_DMA_ACTIVE);
```

## Performance Impact

### Error Case Overhead

When DMA is busy and transfer is rejected:
- **Check**: ~5 CPU cycles
- **Set IRQ**: ~10 CPU cycles  
- **Return**: ~5 CPU cycles
- **Total**: ~20 cycles = ~150ns at 133 MHz

This is well within the 785ns bus timing budget.

### Success Case

When DMA is idle:
- **Setup DMA**: ~50-100 cycles
- **Start transfer**: ~10 cycles
- **Return**: ~5 cycles
- **Total**: ~65-115 cycles = ~500-850ns at 133 MHz

Still within timing budget, and transfer continues in background.

## Summary

| Aspect | Blocking (OLD) | Non-Blocking (NEW) |
|--------|---------------|-------------------|
| **When DMA busy** | Blocks indefinitely | Returns immediately |
| **Bus timing** | ❌ Violated | ✅ Maintained |
| **Error signaling** | None | IRQ_DMA_ERROR |
| **System stability** | ❌ Can hang | ✅ Always stable |
| **6502 response** | Must wait | Can handle error |
| **Overhead** | Variable (µs-ms) | Fixed (~150ns) |

## Related Documents

- `docs/bus_timing.md` - 6502 bus timing requirements
- `docs/dma_implementation.md` - DMA architecture overview
- `docs/dma_copy_behavior.md` - Index behavior during copies
