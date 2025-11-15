# MIA Synchronous Bus Interface

This directory contains the implementation of the synchronous bus protocol for the MIA (Multifunction Interface Adapter).

## Architecture

The bus interface uses a **hybrid PIO + C approach**:

- **PIO (Programmable I/O)**: Handles precise timing of signal sampling (8ns resolution)
- **C Code**: Handles complex logic like address decoding and data preparation

## Key Design Insight

The critical timing constraint is that **OE and WE signals are only valid 30ns after PHI2 rises** (at 530ns in a 1MHz cycle). This means we cannot read these signals immediately when CS becomes active at 200ns.

The solution uses **speculative execution**:
1. C code is triggered at 200ns (when CS is active)
2. C code speculatively prepares READ data during 200-530ns window
3. C code waits for PHI2 to rise (500ns)
4. C code reads OE/WE at 530ns (now valid!)
5. C code determines actual operation type and uses or discards speculative data

## Files

### `bus_sync.pio`
PIO assembly program that implements the synchronous bus protocol:
- Detects PHI2 edges with precise timing
- Samples address at 60ns (after 40ns tADS + 50% margin)
- Samples CS at 200ns (after address mapping logic settles)
- Triggers IRQ to notify C code
- Blocks waiting for C code response
- Drives data bus for READ operations
- Latches data bus for WRITE operations

### `bus_sync_pio.h`
C header file defining the interface:
- Control byte constants (NOP, READ, WRITE)
- GPIO pin definitions
- Function prototypes
- Detailed timing documentation

### `bus_sync_pio.c`
C implementation of the IRQ handler:
- Reads address from PIO
- Speculatively prepares READ data
- Waits for PHI2 to rise
- Reads OE/WE at correct time (530ns)
- Determines operation type
- Pushes response to PIO

### `bus_interface.h` / `bus_interface.c`
High-level bus interface that handles register access:
- Multi-window architecture (Windows A-D)
- Shared register space
- Register read/write handlers
- Integration with indexed memory system

## Timing Budget (1 MHz operation)

| Phase | Time Window | Duration | Purpose |
|-------|-------------|----------|---------|
| Address sampling | 0-60ns | 60ns | Wait for address to settle |
| CS sampling | 60-200ns | 140ns | Wait for address mapping |
| Speculative prep | 200-400ns | 200ns | Decode address, fetch data |
| Wait for PHI2 | 400-500ns | 100ns | Busy-wait for clock rise |
| OE/WE check | 530-540ns | 10ns | Read control signals |
| Response | 540-560ns | 20ns | Push to FIFO |
| PIO drive | 560-985ns | 425ns | Drive data for READ |
| **Total** | **0-985ns** | **985ns** | **Full cycle** |

## FIFO Protocol

### RX FIFO (PIO → C)
- **Address byte**: Pushed at 60ns when address is sampled
- **Data byte**: Pushed at 1000ns for WRITE operations only

### TX FIFO (C → PIO)
- **Control byte**: 0x00 = NOP, 0x01 = READ, 0x02 = WRITE
- **Data byte**: For READ operations only (data to drive on bus)

## Control Flow

```
┌─────────────────────────────────────────────────────────────┐
│                     PIO State Machine                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Wait for PHI2 falling edge (0ns)                         │
│  2. Delay 60ns, sample address → push to RX FIFO            │
│  3. Delay to 200ns, sample CS                                │
│  4. If CS inactive: goto step 1                              │
│  5. Trigger IRQ 0 to notify C code                           │
│  6. Block waiting for control byte from TX FIFO              │
│  7. Pull control byte                                        │
│  8. If NOP: goto step 1                                      │
│  9. If READ: pull data, drive bus, hold for tDHR             │
│ 10. If WRITE: wait for PHI2 fall, latch data, push to FIFO  │
│ 11. Goto step 1                                              │
│                                                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    C IRQ Handler                             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Triggered at 200ns (CS active)                           │
│  2. Read address from RX FIFO                                │
│  3. Speculatively prepare READ data (200-400ns)              │
│  4. Wait for PHI2 to rise (poll GPIO 28)                     │
│  5. Wait 30ns for OE/WE to settle                            │
│  6. Read OE pin (GPIO 19) - now valid!                       │
│  7. Read WE pin (GPIO 18) - now valid!                       │
│  8. Determine operation type:                                │
│     - OE inactive → NOP                                      │
│     - OE active + WE inactive → READ (use speculative data)  │
│     - OE active + WE active → WRITE (discard speculative)    │
│  9. Push control byte to TX FIFO                             │
│ 10. If READ: push data byte to TX FIFO                       │
│ 11. Exit IRQ handler                                         │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## GPIO Pin Assignments

| GPIO | Signal | Direction | Description |
|------|--------|-----------|-------------|
| 0-7 | A0-A7 | Input | Address bus (8-bit) |
| 8-15 | D0-D7 | Bidirectional | Data bus (8-bit) |
| 18 | WE | Input | Write Enable (active low) |
| 19 | OE | Input | Output Enable (active low) |
| 21 | IO0_CS | Input | Chip Select (active low) |
| 28 | PHI2 | Output/Input | Clock (generated by MIA, read by PIO) |

## Initialization

```c
// Initialize in this order:
1. indexed_memory_init();      // Initialize indexed memory system
2. bus_interface_init();        // Initialize bus interface registers
3. clock_control_init();        // Start PHI2 clock generation
4. bus_sync_pio_init();         // Initialize PIO and start state machine
```

## Testing

To verify correct operation:
1. Use logic analyzer to capture PHI2, CS, OE, WE, address, and data signals
2. Verify address is sampled at 60ns (after PHI2 falls)
3. Verify CS is sampled at 200ns
4. Verify OE/WE are read at 530ns (30ns after PHI2 rises)
5. Verify data is valid by 985ns for READ operations
6. Verify data is latched at 1000ns for WRITE operations

## Performance

At 1 MHz operation:
- **READ operations**: ~560ns from CS active to data valid (425ns margin)
- **WRITE operations**: Data latched at 1000ns (optimal timing)
- **C code execution**: ~360ns (well within 785ns budget)
- **Speculative hit rate**: ~80% (most operations are READs)

## Future Optimizations

If 2 MHz operation is needed:
- Reduce speculative preparation time (use lookup tables)
- Optimize C code (inline functions, reduce branches)
- Consider fast/slow path split (simple registers in PIO only)
- Group control signals at consecutive GPIOs for faster reads

## References

- `docs/bus_timing.md` - Detailed timing analysis
- `docs/mia_programming_guide.md` - Programming interface
- W65C02S datasheet - CPU timing specifications
- RP2040 datasheet - PIO capabilities and timing
