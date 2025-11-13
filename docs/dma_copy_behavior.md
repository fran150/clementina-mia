# DMA Copy Operations - Index Behavior

## Summary

Copy operations (`indexed_memory_copy_block`) use indexes as **address pointers only** and do NOT modify them.

## Behavior

### What Happens

```c
// Setup
indexed_memory_set_index_address(10, 0x1000);  // Source
indexed_memory_set_index_address(11, 0x2000);  // Destination

// Copy 100 bytes
indexed_memory_copy_block(10, 11, 100);

// Result:
// - Memory at 0x2000-0x2063 now contains copy of 0x1000-0x1063
// - Index 10 STILL points to 0x1000 (unchanged)
// - Index 11 STILL points to 0x2000 (unchanged)
```

### What Does NOT Happen

- Auto-step flags are **ignored** during copy operations
- Step size is **ignored** during copy operations  
- Direction flags are **ignored** during copy operations
- Indexes remain at their original addresses

## Rationale

### 1. Semantic Clarity

Copy operations are **bulk transfers**, not iterative operations:
- They specify WHERE to copy from/to
- Not HOW to iterate through memory
- Keeping them separate from iteration semantics makes the API clearer

### 2. Predictability

After a copy operation, you know exactly where the indexes point:
```c
// Before copy
uint32_t src = indexed_memory_get_index_address(src_idx);  // 0x1000

indexed_memory_copy_block(src_idx, dst_idx, 1000);

// After copy
uint32_t src_after = indexed_memory_get_index_address(src_idx);  // Still 0x1000
```

### 3. Reusability

Indexes can be reused for multiple operations without resetting:
```c
// Copy same source to multiple destinations
indexed_memory_set_index_address(src_idx, VIDEO_BUFFER);

indexed_memory_set_index_address(dst_idx, BACKUP_1);
indexed_memory_copy_block(src_idx, dst_idx, 4000);

// src_idx still points to VIDEO_BUFFER, can reuse immediately
indexed_memory_set_index_address(dst_idx, BACKUP_2);
indexed_memory_copy_block(src_idx, dst_idx, 4000);
```

### 4. Consistency with DMA Hardware

Hardware DMA controllers don't modify source registers:
- You configure source/destination addresses
- DMA performs the transfer
- Addresses remain unchanged for potential reuse

## Contrast with Read/Write Operations

Individual read/write operations DO respect auto-step:

```c
// Setup with auto-step enabled
indexed_memory_set_index_address(idx, 0x1000);
indexed_memory_set_index_step(idx, 1);
indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP);

// Read advances the index
uint8_t data1 = indexed_memory_read(idx);  // Reads from 0x1000, index becomes 0x1001
uint8_t data2 = indexed_memory_read(idx);  // Reads from 0x1001, index becomes 0x1002

// Write advances the index
indexed_memory_write(idx, 0xAA);  // Writes to 0x1002, index becomes 0x1003
```

## Use Cases

### Video Buffer Management

```c
// Copy front buffer to back buffer
indexed_memory_set_index_address(FRONT_BUF_IDX, FRONT_BUFFER_ADDR);
indexed_memory_set_index_address(BACK_BUF_IDX, BACK_BUFFER_ADDR);

indexed_memory_copy_block(FRONT_BUF_IDX, BACK_BUF_IDX, SCREEN_SIZE);

// Indexes unchanged - can immediately do another operation
// No need to reset indexes
```

### Data Backup

```c
// Backup user data
indexed_memory_set_index_address(USER_IDX, USER_DATA_START);
indexed_memory_set_index_address(BACKUP_IDX, BACKUP_AREA);

indexed_memory_copy_block(USER_IDX, BACKUP_IDX, USER_DATA_SIZE);

// Later, restore from backup (indexes still point to same locations)
indexed_memory_copy_block(BACKUP_IDX, USER_IDX, USER_DATA_SIZE);
```

### Sprite Duplication

```c
// Copy sprite template to multiple OAM entries
indexed_memory_set_index_address(TEMPLATE_IDX, SPRITE_TEMPLATE);

for (int i = 0; i < 10; i++) {
    indexed_memory_set_index_address(OAM_IDX, OAM_BASE + (i * 4));
    indexed_memory_copy_block(TEMPLATE_IDX, OAM_IDX, 4);
    // TEMPLATE_IDX unchanged - can reuse in next iteration
}
```

## Testing

The behavior is explicitly tested:

```c
// Capture addresses before copy
uint32_t src_before = indexed_memory_get_index_address(src_idx);
uint32_t dst_before = indexed_memory_get_index_address(dst_idx);

// Perform copy
indexed_memory_copy_block(src_idx, dst_idx, count);

// Verify indexes unchanged
assert(indexed_memory_get_index_address(src_idx) == src_before);
assert(indexed_memory_get_index_address(dst_idx) == dst_before);
```

## API Summary

| Operation | Modifies Indexes | Respects Auto-Step | Use Case |
|-----------|------------------|-------------------|----------|
| `indexed_memory_read()` | ✅ Yes | ✅ Yes | Sequential reading |
| `indexed_memory_write()` | ✅ Yes | ✅ Yes | Sequential writing |
| `indexed_memory_copy_block()` | ❌ No | ❌ No | Bulk copy (1+ bytes) |

## Future Considerations

If iterative copying is needed in the future, a separate function could be added:

```c
// Hypothetical future function
void indexed_memory_copy_with_step(
    uint8_t src_idx, 
    uint8_t dst_idx, 
    uint16_t count
);
// This WOULD respect auto-step flags and modify indexes
```

However, this is not currently needed and would add complexity.
