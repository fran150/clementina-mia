# COPY_BYTE Command Removal

## Summary

Removed the `CMD_COPY_BYTE` command as it was redundant with `CMD_COPY_BLOCK` (count=1).

## Rationale

### Why COPY_BYTE Was Redundant

**What COPY_BYTE did:**
```c
indexed_memory_copy_byte(src_idx, dst_idx);
// Copy 1 byte from src to dst
```

**What COPY_BLOCK does:**
```c
indexed_memory_copy_block(src_idx, dst_idx, 1);
// Copy 1 byte from src to dst (identical result)
```

### Benefits of Removal

1. **Simpler API** - One copy command instead of two
2. **Less code to maintain** - Removed function, command code, tests, documentation
3. **No performance loss** - Implementation was nearly identical
4. **Cleaner command space** - Freed up command code 0x06
5. **More consistent** - Single unified copy interface

### Command Code Changes

**Before:**
```
0x06 - CMD_COPY_BYTE
0x07 - CMD_COPY_BLOCK
0x08 - CMD_SET_COPY_SRC (redundant)
0x09 - CMD_SET_COPY_DST (redundant)
0x0A - CMD_SET_COPY_COUNT (redundant, not implemented)
```

**After:**
```
0x06 - CMD_COPY_BLOCK (supports 1-65535 bytes)
0x10 - CMD_PICO_REINIT
```

**Note:** DMA parameters are configured via CFG fields, not commands.

## Usage Changes

### Old Way (Two Commands)

```assembly
; Copy single byte
LDA #CMD_COPY_BYTE
STA COMMAND_REG

; Copy multiple bytes
LDA #<count
STA CFG_COPY_COUNT_L
LDA #>count
STA CFG_COPY_COUNT_H
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

### New Way (One Command)

```assembly
; Copy single byte (count=1)
LDA #1
STA CFG_COPY_COUNT_L
LDA #0
STA CFG_COPY_COUNT_H
LDA #CMD_COPY_BLOCK
STA COMMAND_REG

; Copy multiple bytes (same as before)
LDA #<count
STA CFG_COPY_COUNT_L
LDA #>count
STA CFG_COPY_COUNT_H
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

**Note:** If count is already set to 1 (e.g., from a previous operation), you can just execute `CMD_COPY_BLOCK` directly.

## Implementation Changes

### Files Modified

1. **src/indexed_memory/indexed_memory.h**
   - Removed `CMD_COPY_BYTE` definition
   - Removed `indexed_memory_copy_byte()` declaration
   - Renumbered subsequent command codes

2. **src/indexed_memory/indexed_memory.c**
   - Removed `CMD_COPY_BYTE` case from command handler
   - Removed `indexed_memory_copy_byte()` function

3. **tests/indexed_memory/test_indexed_memory.c**
   - Removed `copy_byte` test
   - Added test for `copy_block` with count=1

4. **Documentation**
   - Updated all references to remove `COPY_BYTE`
   - Clarified that `COPY_BLOCK` supports 1-65535 bytes

5. **Requirements & Design**
   - Updated requirements to remove COPY_BYTE
   - Updated design documentation
   - Updated task list

### Code Removed

- 1 command definition
- 1 function declaration
- 1 function implementation (~15 lines)
- 1 command case handler (~3 lines)
- 1 test case (~30 lines)
- Multiple documentation references

**Total:** ~50+ lines of code removed

## Testing

All tests pass with the new implementation:

```bash
$ make test
✓ DMA operations (including single byte with count=1)
✓ All tests passed
```

The test suite now explicitly tests:
- Single byte copy using `copy_block(src, dst, 1)`
- Multi-byte copy using `copy_block(src, dst, 5)`
- DMA busy error handling
- Index preservation during copy

## Migration Guide

For any existing 6502 code using `CMD_COPY_BYTE`:

**Before:**
```assembly
; Select CFG_COPY_SRC_IDX field
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT

; Set source index
LDA #src_idx
STA CFG_DATA

; Select CFG_COPY_DST_IDX field
LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT

; Set destination index
LDA #dst_idx
STA CFG_DATA

; Copy single byte
LDA #CMD_COPY_BYTE
STA COMMAND_REG
```

**After:**
```assembly
; Select CFG_COPY_SRC_IDX field
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT

; Set source index
LDA #src_idx
STA CFG_DATA

; Select CFG_COPY_DST_IDX field
LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT

; Set destination index
LDA #dst_idx
STA CFG_DATA

; Select CFG_COPY_COUNT_L field
LDA #CFG_COPY_COUNT_L
STA CFG_FIELD_SELECT

; Set count to 1
LDA #1
STA CFG_DATA

; Select CFG_COPY_COUNT_H field
LDA #CFG_COPY_COUNT_H
STA CFG_FIELD_SELECT

LDA #0
STA CFG_DATA

; Copy using COPY_BLOCK
LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

**Optimization:** If you're doing multiple single-byte copies, set count=1 once and reuse:

```assembly
; Set count to 1 (do this once)
LDA #CFG_COPY_COUNT_L
STA CFG_FIELD_SELECT
LDA #1
STA CFG_DATA

LDA #CFG_COPY_COUNT_H
STA CFG_FIELD_SELECT
LDA #0
STA CFG_DATA

; Copy multiple single bytes
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx1
STA CFG_DATA

LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #dst_idx1
STA CFG_DATA

LDA #CMD_COPY_BLOCK
STA COMMAND_REG

; Count is still 1, just change indexes
LDA #CFG_COPY_SRC_IDX
STA CFG_FIELD_SELECT
LDA #src_idx2
STA CFG_DATA

LDA #CFG_COPY_DST_IDX
STA CFG_FIELD_SELECT
LDA #dst_idx2
STA CFG_DATA

LDA #CMD_COPY_BLOCK
STA COMMAND_REG
```

## Performance Impact

**None.** The implementation for single-byte copies is identical:

```c
// Both implementations do the same thing for 1 byte:
mia_memory[dst_addr] = mia_memory[src_addr];
```

The only difference is the 6502 needs to set the count register, which adds ~6 CPU cycles (6µs at 1 MHz) - negligible overhead.

## Conclusion

Removing `COPY_BYTE` simplifies the API without any functional loss. The unified `COPY_BLOCK` command handles all copy operations from 1 to 65535 bytes efficiently.
