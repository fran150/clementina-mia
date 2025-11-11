# W65C02S6TPG-14 Bus Timing Specification for MIA Interface

## Overview

This document provides detailed timing analysis for the MIA (Multifunction Interface Adapter) interfacing with the W65C02S6TPG-14 CPU at 1 MHz operation. The timing ensures proper bus protocol compliance and avoids bus contention.

## CPU Specifications

- **CPU Model**: W65C02S6TPG-14 (WDC 65C02 CMOS)
- **Maximum Clock Frequency**: 14 MHz
- **Operating Frequency**: 1 MHz (1000ns cycle time)
- **Clock Duty Cycle**: ~50% (500ns high, 500ns low)

## Key Timing Parameters

### From W65C02S6TPG-14 Datasheet

| Parameter | Description | Value | Notes |
|-----------|-------------|-------|-------|
| **tADS** | Address Setup Time | 40ns max | Time for address to become valid after PHI2 falls |
| **tAH** | Address Hold Time | 10ns min | Address remains valid after PHI2 falls |
| **tDSR** | Data Setup Time (Read) | 15ns min | Data must be stable before CPU samples |
| **tDHR** | Data Hold Time (Read) | 10ns min | Data must remain valid after CPU samples |
| **tACC** | Address Access Time | 70ns min | Time from address valid until data must be valid |
| **tMDS** | Write Data Delay Time | 40ns max | Time for write data to become valid after PHI2 rises |
| **tDHW** | Data Hold Time (Write) | 10ns min | Write data remains valid after PHI2 falls |

### External Logic Delays

| Component | Description | Delay | Notes |
|-----------|-------------|-------|-------|
| **Address Decode** | CS signal generation | ~110ns | External logic decoding address to CS |
| **CS Margin** | Safety margin for CS | +40ns | Conservative estimate |
| **Total CS Delay** | Address valid to CS valid | 200ns | 40ns (tADS) + 80ns (decode) + 80ns (margin) |
| **R/W Decode** | R/W signal propagation | ~30ns | Through external logic |

## Clock Timing Reference

At 1 MHz operation:
- **Full Cycle**: 1000ns
- **PHI2 Low Period**: 0ns to 500ns
- **PHI2 High Period**: 500ns to 1000ns

### Key Time Points (relative to cycle start at PHI2 falling edge)

| Time | Event | Description |
|------|-------|-------------|
| **0ns** | PHI2 falls | Cycle start, address bus begins changing |
| **40ns** | Address valid | Address stable on A0-A15 (tADS max) |
| **200ns** | CS valid | Chip select signals stable after decode |
| **500ns** | PHI2 rises | Clock goes high, R/W and OE become reliable |
| **530ns** | R/W valid | R/W signal stable after propagation delay |
| **540ns** | Write data valid | Data valid on bus for write operations (tMDS) |
| **985ns** | Read data deadline | Data must be valid for read operations (15ns before sample) |
| **1000ns** | PHI2 falls | CPU samples data, next cycle begins, R/W and OE go HIGH (disabled) |
| **1010ns** | Address changes | Address hold complete (tAH), CS may deassert |
| **1015ns** | Read data hold ends | MIA must hold read data for 15ns after PHI2 falls (tDHR) |

---

## READ CYCLE TIMING

### Timeline

```
Time (ns)    Event                           MIA Action
-----------  ------------------------------  ----------------------------------
0            PHI2 falls (cycle start)        Monitor for CS assertion
             Address bus changing            
             
40           Address valid (tADS)            Address stable on A0-A15
             
200          CS valid (conservative)         ** MIA DETECTS SELECTION **
                                            - Capture address from A0-A7
                                            - Begin register decode
                                            - Speculatively fetch data (assuming read)
                                            - Stage data in buffer (ready for either operation)
                                            
500          PHI2 rises                      R/W signal becoming valid
             
530          R/W valid (with decode delay)   ** MIA CONFIRMS READ **
                                            - Check R/W = HIGH (confirms read operation)
                                            - Check OE = LOW (output enable)
                                            - Data already prepared during 200-530ns window
                                            - Enable D0-D7 output drivers
                                            - Drive prepared data onto bus
                                            
985          Data must be valid              Data stable on D0-D7
             (15ns before sample)            
             
1000         PHI2 falls                      ** CPU SAMPLES DATA **
             CPU latches data                R/W and OE go HIGH (disabled)
             Next cycle begins               Data must remain stable
             
1010         Address changes (tAH)           CS may deassert
             
1015         Data hold complete (tDHR)       ** MIA RELEASES BUS **
                                            - Hold data for 15ns after PHI2 falls
                                            - Tri-state D0-D7 outputs
                                            - Configure D0-D7 as inputs
```

### MIA Response Time Budget

| Phase | Start | End | Duration | Purpose |
|-------|-------|-----|----------|---------|
| **Speculative Preparation** | 200ns | 530ns | **330ns** | Decode address, speculatively fetch data (assuming read) |
| **Confirmed Preparation** | 530ns | 985ns | **455ns** | After R/W confirms read, finalize and drive data |
| **Total Preparation** | 200ns | 985ns | **785ns** | Full time available from CS valid to data deadline |
| **Data Valid** | 985ns | 1015ns | **30ns** | Hold data stable for CPU sampling (15ns required + 5ns safety margin) |
| **Drive Window** | 530ns | 1015ns | **485ns** | Data can be driven anytime in this window but must remain stable once driven |

**Speculative Preparation Strategy**: The MIA can optimize performance by speculatively preparing for a read operation during the 200-530ns window (before R/W is valid). This involves:
- Decoding the address to determine which register is being accessed
- Fetching data from internal memory
- Staging the data in a buffer

At 530ns when R/W becomes valid:
- **If READ (R/W = HIGH)**: Use the pre-fetched data and drive it onto the bus immediately
- **If WRITE (R/W = LOW)**: Discard the speculative data and prepare to receive write data

This speculative approach maximizes the time available for memory access and data preparation, reducing the critical path after R/W confirmation.

### Critical Requirements for READ

1. **Data must be valid by 985ns** (15ns before PHI2 falls)
2. **Data must remain valid until 1015ns** (15ns after PHI2 falls, tDHR, plus 5ns safety margin)
3. **Data can be driven anytime between 530ns and 985ns** - but must remain stable once driven
4. **MIA has 785ns from CS valid** to prepare data (200ns to 985ns)
5. **MIA has 455ns from R/W confirmation** to drive data (530ns to 985ns)
6. **OE signal controls when MIA drives the bus** - only drive when OE is LOW
7. **OE goes HIGH at 1000ns** (with PHI2 falling edge) - MIA must continue driving until 1015ns
8. **Implementation flexibility**: Drive early and hold, or drive just before deadline - both are valid

---

## WRITE CYCLE TIMING

### Timeline

```
Time (ns)    Event                           MIA Action
-----------  ------------------------------  ----------------------------------
0            PHI2 falls (cycle start)        Monitor for CS assertion
             Address bus changing            D0-D7 configured as inputs
             
40           Address valid (tADS)            Address stable on A0-A15
             
200          CS valid (conservative)         ** MIA DETECTS SELECTION **
                                            - Capture address from A0-A7
                                            - Begin register decode
                                            - Speculatively prepare for both read/write
                                            - Keep D0-D7 as inputs (safe default)
                                            
500          PHI2 rises                      R/W signal becoming valid
             CPU begins driving data         
             
530          R/W valid (with decode delay)   ** MIA CONFIRMS WRITE **
                                            - Check R/W = LOW (confirms write operation)
                                            - Check OE = HIGH (disabled)
                                            - Confirm D0-D7 are inputs (already set)
                                            - Discard any speculatively fetched read data
                                            - Prepare to sample incoming write data
                                            
540          Write data valid (tMDS)         ** CPU DATA NOW VALID **
                                            - Data stable on D0-D7
                                            - MIA can begin sampling
                                            
540-1000     Data sampling window            MIA can read D0-D7 anytime in window
                                            Best practice: sample at PHI2 falling edge
                                            
1000         PHI2 falls                      ** MIA LATCHES DATA **
             Next cycle begins               - Sample D0-D7 on falling edge
             R/W and OE go HIGH              - Data remains valid for tDHW (10ns)
             
1010         Data hold complete (tDHW)       ** MIA PROCESSES WRITE **
             Address changes (tAH)           - Data capture complete
             CS may deassert                 - Begin write operation
                                            - Update internal registers/memory
```

### MIA Response Time Budget

| Phase | Start | End | Duration | Purpose |
|-------|-------|-----|----------|---------|
| **Speculative Preparation** | 200ns | 530ns | **330ns** | Decode address, prepare for potential write (or read) |
| **Confirmed Preparation** | 530ns | 540ns | **10ns** | After R/W confirms write, finalize write setup |
| **Data Sampling Window** | 540ns | 1010ns | **470ns** | Sample data from D0-D7 (best at 1000ns falling edge) |
| **Processing** | 1010ns+ | Flexible | Variable | Process write operation |

**Note**: During the 200-530ns speculative preparation window, the MIA doesn't know if the operation will be a read or write. It can speculatively fetch data (assuming read), but must be prepared to discard this and switch to write mode when R/W confirms at 530ns.

### Critical Requirements for WRITE

1. **MIA must NOT drive D0-D7 during write** (540ns to 1010ns)
2. **OE will be HIGH (disabled)** during write operations
3. **Data is valid from 540ns to 1010ns** - CPU drives data during this window
4. **MIA should latch data on PHI2 falling edge** (1000ns) for most reliable capture
5. **Data remains valid until 1010ns** (tDHW = 10ns after PHI2 falls)
6. **Write processing can extend beyond cycle** - no strict deadline
7. **CS may deassert at 1010ns** (10ns after address changes)

---

## Bus Contention Avoidance

### OE (Output Enable) Signal Control

The **OE signal is the primary mechanism** for preventing bus contention:

- **OE = LOW (active)**: MIA should drive the data bus (READ operation)
- **OE = HIGH (inactive)**: MIA must tri-state the data bus (WRITE operation or no access)

**Important Exception for READ operations**: 
- OE goes HIGH at 1000ns (with PHI2 falling edge)
- However, MIA must continue driving data until 1015ns to meet tDHR (15ns data hold time)
- This means MIA drives for 15ns after OE deasserts
- This is safe because the CPU is still sampling/holding the data and won't drive the bus until the next cycle

### MIA Bus Control Strategy

```
┌────────────────────────────────────────────────────────────┐
│ MIA Bus Control State Machine                              │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  DEFAULT STATE: D0-D7 configured as inputs (tri-stated)    │
│                                                            │
│  On CS assertion (200ns):                                  │
│    ├─ Capture address                                      │
│    ├─ Decode register                                      │
│    └─ Prepare data (speculative)                           │
│                                                            │
│  On R/W valid (530ns):                                     │
│    ├─ IF R/W = HIGH (READ):                                │
│    │    ├─ Wait for OE = LOW                               │
│    │    ├─ Configure D0-D7 as outputs                      │
│    │    └─ Drive prepared data                             │
│    │                                                       │
│    └─ IF R/W = LOW (WRITE):                                │
│         ├─ Verify OE = HIGH                                │
│         ├─ Keep D0-D7 as inputs                            │
│         └─ Prepare to sample data                          │
│                                                            │
│  At PHI2 falling edge (1000ns):                            │
│    ├─ R/W and OE go HIGH                                   │
│    │                                                       │
│    ├─ IF was READ:                                         │
│    │    ├─ Continue driving data for tDHR (15ns)           │
│    │    └─ Tri-state at 1015ns                             │
│    │                                                       │
│    └─ IF was WRITE:                                        │
│         ├─ Latch data from D0-D7 on falling edge           │
│         ├─ Data valid until 1010ns (tDHW)                  │
│         └─ Process write operation                         │
│                                                            │
│  At 1010ns:                                                │
│    └─ CS may deassert (address changes, tAH complete)      │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

### Bus Drive Periods

| Operation | MIA Drives Bus | CPU Drives Bus | Both Tri-stated |
|-----------|----------------|----------------|-----------------|
| **READ** | 530ns - 1015ns | Never | 0ns - 530ns, 1015ns+ |
| **WRITE** | Never | 540ns - 1010ns | 0ns - 540ns, 1010ns+ |
| **No Access** | Never | Never | Always |

**Note**: During READ operations, OE goes HIGH at 1000ns but MIA must continue driving data until 1015ns to meet tDHR (15ns data hold time).

### Safety Margins

Between consecutive cycles, there are safe transition periods:

1. **READ → WRITE transition**:
   - MIA stops driving: 1015ns (cycle N)
   - CPU starts driving: 540ns (cycle N+1)
   - **Safe gap**: 525ns

2. **WRITE → READ transition**:
   - CPU stops driving: 1010ns (cycle N)
   - MIA starts driving: 530ns (cycle N+1)
   - **Safe gap**: 520ns

3. **Any → No Access**:
   - READ: MIA stops at 1015ns
   - WRITE: CPU stops at 1010ns
   - **Safe**: Both tri-stated immediately after hold times

**Key Point**: By following OE signal timing (with the exception of the 15ns data hold extension for reads), bus contention is automatically avoided. The MIA must drive 15ns beyond OE deassertion during reads to comply with tDHR, but this is safe as the CPU doesn't drive the bus during this period.

---

## Implementation Considerations

### Timing Margins at 1 MHz

All timing requirements have comfortable margins at 1 MHz operation:

| Requirement | Available Time | Needed Time | Margin | Status |
|-------------|----------------|-------------|--------|--------|
| Read data preparation | 785ns | ~100-200ns | 585ns+ | ✓ Excellent |
| Read data drive | 455ns | ~50ns | 405ns | ✓ Excellent |
| Read data hold | 15ns | 15ns | 0ns | ✓ Exact |
| Write data latch | At falling edge | ~10ns | N/A | ✓ Excellent |
| Bus tri-state | 520ns+ | ~10ns | 510ns+ | ✓ Excellent |

### Recommended Implementation Approach

#### Option 1: PIO State Machine (Recommended)

Use Raspberry Pi Pico 2 W PIO for time-critical bus interface:

**Advantages**:
- Deterministic timing (8ns per instruction at 125 MHz PIO clock)
- Hardware-level response to CS, OE, R/W signals
- No CPU intervention for bus cycles
- Can handle back-to-back cycles reliably

**PIO State Machine Logic**:
```
1. WAIT for CS = LOW (active)
2. IN pins (capture A0-A7)
3. WAIT for PHI2 = HIGH (or use OE as proxy)
4. IN pins (capture R/W)
5. JMP based on R/W:
   - If READ: OUT pins (drive data), WAIT for OE = HIGH, tri-state
   - If WRITE: IN pins (capture data), process
6. Loop back to step 1
```

**Data Preparation**:
- CPU pre-computes read responses during idle time
- Stores in PIO TX FIFO or DMA buffer
- PIO pulls data and drives bus when needed

#### Option 2: Interrupt-Driven CPU

Use GPIO interrupts on CS edge:

**Advantages**:
- Simpler to program
- More flexible logic
- Easier debugging

**Challenges**:
- Interrupt latency must be < 785ns (achievable at 150 MHz)
- Must handle back-to-back cycles
- More CPU overhead

**Interrupt Handler**:
```c
void cs_interrupt_handler() {
    // Capture address (immediate)
    uint8_t addr = read_address_bus();
    
    // Decode and fetch data (~100-200ns)
    uint8_t data = decode_and_fetch(addr);
    
    // Wait for R/W valid (~330ns from CS)
    while (!rw_valid()) { /* spin */ }
    
    if (is_read()) {
        // Wait for OE active
        while (OE_HIGH) { /* spin */ }
        
        // Drive data
        drive_data_bus(data);
        
        // Wait for PHI2 falling edge (1000ns)
        while (PHI2_HIGH) { /* spin */ }
        
        // Hold data for tDHR (15ns after PHI2 falls)
        delay_ns(15);
        
        // Tri-state
        tristate_data_bus();
    } else {
        // Wait for PHI2 falling edge to latch data
        while (PHI2_HIGH) { /* spin */ }
        
        // Latch data on falling edge
        uint8_t write_data = read_data_bus();
        
        // Process write (data valid for 10ns more)
        process_write(addr, write_data);
    }
}
```

#### Option 3: Hybrid PIO + CPU

**Best of both worlds**:
- PIO handles bus protocol and timing
- CPU handles data preparation and processing
- PIO and CPU communicate via FIFO

### Signal Connections Required

| Signal | GPIO | Direction | Purpose |
|--------|------|-----------|---------|
| A0-A7 | 0-7 | Input | Address bus (low 8 bits) |
| D0-D7 | 8-15 | Bidirectional | Data bus |
| PICOHIRAM | 16 | Output | Bank MIA in/out of high memory |
| Reset | 17 | Output | Reset 6502 CPU |
| WE | 18 | Input | Write Enable (active low) |
| OE | 19 | Input | Output Enable (active low) - **CRITICAL** |
| HIRAM_CS | 20 | Input | High RAM chip select (active low) |
| IO0_CS | 21 | Input | I/O chip select (active low) |
| R/W | TBD | Input | Read/Write signal - **REQUIRED** |
| PHI2 | TBD | Input | Clock phase 2 - **OPTIONAL** (MIA generates it) |
| IRQ | 26 | Output | Interrupt request to CPU |

**Note**: R/W signal connection is **required** but not currently in requirements. PHI2 input is **optional** since MIA generates the clock and knows its state internally.

### Timing Verification

To verify timing in implementation:

1. **Use logic analyzer** to capture:
   - CS assertion to data valid (should be < 785ns)
   - OE active to data drive (should be < 50ns)
   - Data hold after PHI2 falls (should be > 10ns)

2. **Test at multiple frequencies**:
   - Start at 100 kHz (boot phase) - 10µs cycle
   - Verify at 1 MHz (normal operation) - 1µs cycle
   - Test margin at 2 MHz if possible - 500ns cycle

3. **Stress test scenarios**:
   - Back-to-back reads
   - Back-to-back writes
   - Alternating read/write
   - Rapid register access patterns

---

## Timing at Different Clock Frequencies

### Boot Phase: 100 kHz (10,000ns cycle)

| Event | Time | Notes |
|-------|------|-------|
| PHI2 falls | 0ns | Cycle start |
| Address valid | 40ns | Same absolute time |
| CS valid | 200ns | Same absolute time |
| PHI2 rises | 5000ns | 50% duty cycle |
| R/W valid | 5030ns | Same delay |
| Read data deadline | 9985ns | 15ns before sample |
| PHI2 falls | 10000ns | CPU samples |

**Margins**: All timing requirements easily met with 100x more time.

### Normal Operation: 1 MHz (1,000ns cycle)

See detailed timing above. All requirements met with comfortable margins.

### Maximum Theoretical: 2 MHz (500ns cycle)

| Event | Time | Notes |
|-------|------|-------|
| PHI2 falls | 0ns | Cycle start |
| Address valid | 40ns | Same absolute time |
| CS valid | 200ns | Same absolute time |
| PHI2 rises | 250ns | 50% duty cycle |
| R/W valid | 280ns | Same delay |
| Read data deadline | 485ns | 15ns before sample |
| PHI2 falls | 500ns | CPU samples |

**Margins**: 
- Read preparation: 200ns to 485ns = **285ns** (still adequate)
- Confirmed read: 280ns to 485ns = **205ns** (tight but possible)

**Recommendation**: 1 MHz is the target frequency with excellent margins. 2 MHz would require careful optimization but is theoretically possible.

---

## Summary

### Key Takeaways

1. **At 1 MHz, timing is very comfortable** - MIA has 785ns to prepare read data and 470ns to sample write data

2. **OE signal is critical** - It prevents bus contention by controlling when MIA drives the bus

3. **R/W signal must be connected** - Currently missing from requirements, needed to distinguish read from write

4. **Speculative preparation is possible** - MIA can start work at CS assertion (200ns) before knowing operation type (530ns)

5. **PIO implementation recommended** - Provides deterministic timing and minimal CPU overhead

6. **All timing margins are adequate** - No critical timing paths at 1 MHz operation

### Timing Budget Summary

| Operation | Phase | Available Time | Status |
|-----------|-------|----------------|--------|
| **READ** | Preparation | 785ns | ✓ Excellent |
| **READ** | Confirmed response | 455ns | ✓ Excellent |
| **READ** | Data hold | 15ns | ✓ Exact (tDHR) |
| **WRITE** | Preparation | 340ns | ✓ Excellent |
| **WRITE** | Data latch window | 470ns | ✓ Excellent |
| **WRITE** | Processing | Flexible | ✓ No constraint |

---

## References

- W65C02S Datasheet: https://www.westerndesigncenter.com/wdc/documentation/w65c02s.pdf
- WDC 65C02 Microprocessor Datasheet (general timing information)
- Raspberry Pi Pico 2 W Datasheet (PIO timing specifications)

---

*Document Version: 1.0*  
*Last Updated: 2025-11-09*  
*Author: MIA Design Team*
