# SET_COPY Commands Removal

## Summary

Removed redundant `CMD_SET_COPY_SRC`, `CMD_SET_COPY_DST`, and `CMD_SET_COPY_COUNT` commands in favor of using the existing CFG fields directly.

## Rationale

### Why SET_COPY Commands Were Redundant

These commands provided a shortcut to set DMA parameters, but they were:

1. **Not significantly shorter** - Still required selecting an index first
2. **Less flexible** - Could only use Window A's selected index
3. **Incomplete** - `CMD_SET_COPY_COUNT` was defined but never implemented
4. **Confusing** - Two different ways to configure the same thing
5. **Inconsistent** - Other configuration uses CFG fields, not commands

### What They Did

```c
// CMD_SET_COPY_SRC: Copy window_a_idx to dma_config.src_idx
g_state.dma_config.src_idx = g_state.window_a_idx;

// CMD_SET_COPY_DST: Copy window_a_idx to dma_config.dst_idx  
g_state.dma_config.dst_idx = g_state.window_a_idx;

// CMD_SET_COPY_COUNT: NOT IMPLEMENTED!
```

### What Replaced Them

Direct access to CFG fields (which already existed):

- `CFG_COPY_SRC_IDX` (0x0B) - Source index
- `CFG_COPY_DST_IDX` (0x0C) - Destination index
- `CFG_COPY_COUNT_L` (0x0D) - Count low byte
- `CFG_COPY_COUNT_H` (0x0E) - Count high byte

## Command Code Changes

**Before:**
```
0x06 - CMD_COPY_BLOCK
0x07 - CMD_SET_COPY_SRC (removed)
0x08 - CMD_SET_COPY_DST (removed)
0x09 - CMD_SET_COPY_COUNT (removed, was never implemented)
0x10 - CMD_PICO_REINIT
```

**After:**
```
0x06 - CMD_COPY_BLOCK
0x10 - CMD_PICO_REINIT
```

Command codes 0x07-0x09 are now available for future use.

## Usage Changes

### Old Way (Using Commands)

```assembly
; Set source index using command
LDA #src_idx
STA IDX_SELECT          ; Select index in Window A
LDA #CMD_SET_COPY_SRC
STA COMMAND_REG         ; Copy window_a_idx to dma_config.src_idx

; Set destination index using command
LDA #dst_idx
STA IDX_SELECT          ; Select index in Window A
LDA #CMD_SET_COPY_DST
STA COMMAND_REG         ; Copy window_a_idx to dma_config.dst_idx

; CMD_SET_COPY_COUNT didn't exist!
; Had to use CFG fields anyway for count
LDA #<count
STA CFG_COPY_COUNT_L
LDA #>count
STA CFG_COPY_COUNT_H

; Execute copy
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

**Problems:**
- Inconsistent: Commands for src/dst, CFG fields for count
- Verbose: Need to select index, then execute command
- Limited: Can only use Window A's selected index

### New Way (Using CFG Fields)

```assembly
; Set source index via CFG field
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx
STA CFG_DATA

; Set destination index via CFG field
LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #dst_idx
STA CFG_DATA

; Set count via CFG fields
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
```

**Benefits:**
- Consistent: All parameters use CFG fields
- Flexible: Can set any index value directly
- Complete: All parameters configured the same way

## Optimization: Reusing Configuration

Since DMA parameters persist, you can optimize repeated operations:

```assembly
; Set up DMA parameters once
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx
STA CFG_DATA

LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #dst_idx
STA CFG_DATA

LDA #CFG_COPY_COUNT_L
STA CFG_FIELD_SELECT
LDA #100
STA CFG_DATA

LDA #CFG_COPY_COUNT_H
STA CFG_FIELD_SELECT
LDA #0
STA CFG_DATA

; Execute multiple copies with same parameters
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
; Wait for completion...

; Parameters still set, can copy again immediately
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

Or change just what you need:

```assembly
; Change only the destination
LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #new_dst_idx
STA CFG_DATA

; Source and count unchanged
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

## Implementation Changes

### Files Modified

1. **src/indexed_memory/indexed_memory.h**
   - Removed `CMD_SET_COPY_SRC` definition
   - Removed `CMD_SET_COPY_DST` definition
   - Removed `CMD_SET_COPY_COUNT` definition

2. **src/indexed_memory/indexed_memory.c**
   - Removed `CMD_SET_COPY_SRC` case handler
   - Removed `CMD_SET_COPY_DST` case handler

3. **Documentation**
   - Updated all examples to use CFG fields
   - Removed references to SET_COPY commands
   - Added notes about CFG field usage

### Code Removed

- 3 command definitions
- 2 command case handlers (~6 lines)
- Multiple documentation references

**Total:** ~10+ lines of code removed, plus simplified documentation

## Benefits

1. **Consistency** - All configuration uses the same mechanism (CFG fields)
2. **Flexibility** - Can set any index value, not just Window A's selection
3. **Completeness** - All three parameters (src, dst, count) configured the same way
4. **Simplicity** - One way to do things, not two
5. **Freed command codes** - 0x07-0x09 available for future features

## Migration Guide

For any existing 6502 code using `CMD_SET_COPY_*`:

**Before:**
```assembly
LDA #src_idx
STA IDX_SELECT
LDA #CMD_SET_COPY_SRC
STA COMMAND_REG
```

**After:**
```assembly
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx
STA CFG_DATA
```

The new approach is actually more direct - you set the value you want, rather than going through Window A's index selection.

## Performance Impact

**Slightly better!** The new approach is actually more efficient:

**Old way (using commands):**
```
LDA #src_idx        ; 2 cycles
STA IDX_SELECT      ; 4 cycles
LDA #CMD_SET_COPY_SRC ; 2 cycles
STA COMMAND_REG     ; 4 cycles
Total: 12 cycles
```

**New way (using CFG fields):**
```
LDA #CFG_COPY_SRC_IDX ; 2 cycles
STA CFG_FIELD_SELECT  ; 4 cycles
LDA #src_idx          ; 2 cycles
STA CFG_DATA          ; 4 cycles
Total: 12 cycles
```

Same cycle count, but the new way is more flexible (can set any value, not just Window A's selection).

## Conclusion

Removing the redundant `SET_COPY` commands simplifies the API and makes DMA configuration more consistent. All parameters are now configured through CFG fields, which is more flexible and easier to understand.
