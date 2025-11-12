# Bus Interface Module

## Overview

The Bus Interface module provides the 6502 bus interface for the MIA indexed memory system. It handles register access for the dual-window architecture at addresses $C000-$C00F (mirrored throughout $C000-$C3FF).

## Architecture

### Memory Map

- **Window A**: $C000-$C007 (primary interface)
- **Window B**: $C008-$C00F (secondary interface)
- **Mirroring**: 16-byte register window repeats throughout $C000-$C3FF

### Register Layout (both windows)

| Offset | Register | Description |
|--------|----------|-------------|
| +0 | IDX_SELECT | Select active index (0-255) |
| +1 | DATA_PORT | Read/write byte at current index address with auto-step |
| +2 | CFG_FIELD_SELECT | Select configuration field |
| +3 | CFG_DATA | Read/write selected configuration field |
| +4 | COMMAND | Issue control commands |
| +5 | STATUS | Device status bits (shared) |
| +6 | IRQ_CAUSE_LOW | Interrupt source identification low byte (shared) |
| +7 | IRQ_CAUSE_HIGH | Interrupt source identification high byte (shared) |

## Files

- **bus_interface.h**: Header file with register constants, macros, and function prototypes
- **bus_interface.c**: Implementation of bus interface handlers
- **bus_interface_test.h**: Test interface
- **bus_interface_test.c**: Test implementation

## Key Features

### Address Decoding

- Detects addresses in $C000-$C3FF range
- Extracts register offset (0-15) from address
- Handles 16-byte register window mirroring

### Window Detection

- Window A: bit 3 = 0 (offsets 0-7)
- Window B: bit 3 = 1 (offsets 8-15)
- Window priority: Window A takes precedence on conflicts

### Helper Macros

```c
IS_INDEXED_INTERFACE_ADDR(addr)  // Check if address is in $C000-$C3FF
GET_REGISTER_OFFSET(addr)        // Extract register offset (0-15)
IS_WINDOW_B(addr)                // Detect Window B access
GET_WINDOW_REGISTER_OFFSET(addr) // Get register offset within window (0-7)
```

### Helper Functions

```c
bool bus_interface_is_indexed_addr(uint16_t address);
bool bus_interface_is_window_b(uint16_t address);
uint8_t bus_interface_get_register_offset(uint16_t address);
```

## Main Entry Points

### bus_interface_init()

Initializes the bus interface module:
- Sets up GPIO pins for bus interface
- Initializes PIO state machines
- Configures interrupt handlers

### bus_interface_read(uint16_t address)

Handles READ operations from the 6502 bus:
- Validates address is in indexed interface range
- Determines which window is being accessed
- Routes to appropriate register handler
- Returns data byte to 6502

### bus_interface_write(uint16_t address, uint8_t data)

Handles WRITE operations from the 6502 bus:
- Validates address is in indexed interface range
- Determines which window is being accessed
- Routes to appropriate register handler
- Updates internal state

## Implementation Status

### Completed (Task 5.1)

- ✅ Register address constants defined
- ✅ Window detection macros implemented
- ✅ Helper functions created
- ✅ Main entry point prototypes defined
- ✅ Basic module structure with dispatcher
- ✅ Test framework created

### Pending (Future Tasks)

- ⏳ Register handler implementations (Tasks 6-10)
- ⏳ PIO state machine integration (Task 11)
- ⏳ GPIO pin configuration
- ⏳ Interrupt handler setup
- ⏳ Timing optimization for 1 MHz operation

## Testing

Run bus interface tests:

```c
#include "bus_interface/bus_interface_test.h"

bool result = run_bus_interface_tests();
```

Tests verify:
- Address range detection ($C000-$C3FF)
- Window detection (A vs B)
- Register offset extraction
- Address mirroring throughout range

## Requirements Satisfied

This module satisfies requirements from the design document:

- **Requirement 5.1**: Indexed interface responds to $C000-$C3FF range
- **Requirement 5.2**: Dual-window interface at $C000-$C007 and $C008-$C00F
- **Requirement 5.3**: Window B identical to Window A
- **Requirement 5.4**: Window A priority on simultaneous access

## Next Steps

1. Implement register handlers (Tasks 6-10):
   - IDX_SELECT read/write
   - DATA_PORT read/write
   - CFG_FIELD_SELECT read/write
   - CFG_DATA read/write
   - COMMAND write
   - STATUS read
   - IRQ_CAUSE read/write

2. Integrate PIO state machines (Task 11):
   - Bus protocol timing
   - READ/WRITE cycle handling
   - Fast path optimization

3. Complete system integration:
   - GPIO pin configuration
   - Interrupt handlers
   - Timing validation
