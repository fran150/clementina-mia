# W65C02S6TPG-14 Bus Timing Specification for MIA Interface

## Overview

This document provides detailed timing analysis for the MIA (Multifunction Interface Adapter) interfacing with the W65C02S6TPG-14 CPU at 1 MHz operation. The MIA operates as a synchronous component, generating the clock signal and using it to precisely time bus operations. This synchronous design avoids reacting to transient signals during address/control line settling periods.

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
| **0ns** | PHI2 falls | **MIA detects clock low**, cycle start, address bus begins changing |
| **40ns** | Address valid | Address stable on A0-A15 (tADS max) |
| **60ns** | **MIA reads address** | **MIA samples address bus (40ns + 50% safety margin)** |
| **200ns** | **MIA reads CS** | **MIA samples CS after address mapping logic settles** |
| **500ns** | PHI2 rises | **MIA detects clock high**, decoding for OE and R/W starts |
| **530ns** | **MIA reads R/W and OE** | **MIA samples R/W and OE (30ns after PHI2 high)** |
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
0            PHI2 falls (cycle start)        ** MIA DETECTS CLOCK LOW **
             Address bus changing            - Wait for address to settle
             
40           Address valid (tADS)            Address stable on A0-A15
             
60           ** MIA READS ADDRESS **         ** MIA SAMPLES ADDRESS BUS **
                                            - Read A0-A7 (60ns = 40ns + 50% margin)
                                            - Begin address decode
                                            
200          ** MIA READS CS **              ** MIA DETECTS SELECTION **
                                            - Sample CS signal (after mapping logic)
                                            - If CS active: begin register decode
                                            - Speculatively fetch data (assuming read)
                                            - Stage data in buffer
                                            - If CS inactive: return to wait state
                                            
500          PHI2 rises                      ** MIA DETECTS CLOCK HIGH **
                                            - R/W and OE signals becoming valid
                                            
530          ** MIA READS R/W & OE **        ** MIA CONFIRMS OPERATION TYPE **
                                            - Sample R/W signal (30ns after PHI2 high)
                                            - Sample OE signal
                                            - If R/W = HIGH (read):
                                              * Data already prepared during 200-530ns
                                              * Enable D0-D7 output drivers
                                              * Drive prepared data onto bus
                                            - If R/W = LOW (write):
                                              * Discard speculative data
                                              * Prepare to receive write data
                                            
985          Data must be valid              Data stable on D0-D7 (for reads)
             (15ns before sample)            
             
1000         PHI2 falls                      ** CPU SAMPLES DATA **
             CPU latches data                R/W and OE go HIGH (disabled)
             Next cycle begins               ** MIA RETURNS TO WAIT STATE **
                                            - For reads: data must remain stable
                                            - For writes: latch incoming data
             
1010         Address changes (tAH)           CS may deassert
             
1015         Data hold complete (tDHR)       ** MIA RELEASES BUS (reads only) **
                                            - Hold data for 15ns after PHI2 falls
                                            - Tri-state D0-D7 outputs
                                            - Configure D0-D7 as inputs
                                            - Return to wait for clock low
```

### MIA Response Time Budget

| Phase | Start | End | Duration | Purpose |
|-------|-------|-----|----------|---------|
| **Address Sampling** | 0ns | 60ns | **60ns** | Wait for address to settle, then sample (40ns + 50% margin) |
| **CS Sampling** | 60ns | 200ns | **140ns** | Wait for address mapping logic, then sample CS |
| **Speculative Preparation** | 200ns | 530ns | **330ns** | Decode address, speculatively fetch data (assuming read) |
| **R/W Sampling** | 500ns | 530ns | **30ns** | Wait for PHI2 high, then sample R/W and OE |
| **Confirmed Preparation** | 530ns | 985ns | **455ns** | After R/W confirms read, finalize and drive data |
| **Total Preparation** | 200ns | 985ns | **785ns** | Full time available from CS valid to data deadline |
| **Data Valid** | 985ns | 1015ns | **30ns** | Hold data stable for CPU sampling (15ns required + 5ns safety margin) |
| **Drive Window** | 530ns | 1015ns | **485ns** | Data can be driven anytime in this window but must remain stable once driven |

**Synchronous Operation Strategy**: The MIA operates synchronously with the clock it generates:
1. **Wait for PHI2 low** (0ns) - Detect clock falling edge
2. **Sample address at 60ns** - After 40ns settling + 50% margin
3. **Sample CS at 200ns** - After address mapping logic settles
4. **Speculative preparation** (200-530ns) - Decode and fetch data assuming read
5. **Wait for PHI2 high** (500ns) - Detect clock rising edge
6. **Sample R/W and OE at 530ns** - 30ns after PHI2 high
7. **Confirm operation type**:
   - **If READ (R/W = HIGH)**: Use pre-fetched data and drive bus immediately
   - **If WRITE (R/W = LOW)**: Discard speculative data and prepare to receive
8. **Return to wait state** - Wait for next PHI2 low

This synchronous approach eliminates sensitivity to transient signals during address/control line settling, ensuring reliable operation by sampling signals only at precisely timed moments.

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
0            PHI2 falls (cycle start)        ** MIA DETECTS CLOCK LOW **
             Address bus changing            - D0-D7 configured as inputs
                                            - Wait for address to settle
             
40           Address valid (tADS)            Address stable on A0-A15
             
60           ** MIA READS ADDRESS **         ** MIA SAMPLES ADDRESS BUS **
                                            - Read A0-A7 (60ns = 40ns + 50% margin)
                                            - Begin address decode
             
200          ** MIA READS CS **              ** MIA DETECTS SELECTION **
                                            - Sample CS signal (after mapping logic)
                                            - If CS active: begin register decode
                                            - Speculatively prepare (assuming read)
                                            - Keep D0-D7 as inputs (safe default)
                                            - If CS inactive: return to wait state
                                            
500          PHI2 rises                      ** MIA DETECTS CLOCK HIGH **
             CPU begins driving data         - R/W and OE signals becoming valid
             
530          ** MIA READS R/W & OE **        ** MIA CONFIRMS WRITE **
                                            - Sample R/W = LOW (write operation)
                                            - Sample OE = HIGH (disabled)
                                            - Confirm D0-D7 are inputs (already set)
                                            - Discard any speculatively fetched data
                                            - Prepare to sample incoming write data
                                            
540          Write data valid (tMDS)         ** CPU DATA NOW VALID **
                                            - Data stable on D0-D7
                                            - MIA can begin sampling
                                            
540-1000     Data sampling window            MIA can read D0-D7 anytime in window
                                            Best practice: sample at PHI2 falling edge
                                            
1000         PHI2 falls                      ** MIA LATCHES DATA **
             Next cycle begins               - Sample D0-D7 on falling edge
             R/W and OE go HIGH              - Data remains valid for tDHW (10ns)
                                            ** MIA RETURNS TO WAIT STATE **
             
1010         Data hold complete (tDHW)       ** MIA PROCESSES WRITE **
             Address changes (tAH)           - Data capture complete
             CS may deassert                 - Begin write operation
                                            - Update internal registers/memory
                                            - Return to wait for clock low
```

### MIA Response Time Budget

| Phase | Start | End | Duration | Purpose |
|-------|-------|-----|----------|---------|
| **Address Sampling** | 0ns | 60ns | **60ns** | Wait for address to settle, then sample (40ns + 50% margin) |
| **CS Sampling** | 60ns | 200ns | **140ns** | Wait for address mapping logic, then sample CS |
| **Speculative Preparation** | 200ns | 530ns | **330ns** | Decode address, prepare for potential write (or read) |
| **R/W Sampling** | 500ns | 530ns | **30ns** | Wait for PHI2 high, then sample R/W and OE |
| **Confirmed Preparation** | 530ns | 540ns | **10ns** | After R/W confirms write, finalize write setup |
| **Data Sampling Window** | 540ns | 1010ns | **470ns** | Sample data from D0-D7 (best at 1000ns falling edge) |
| **Processing** | 1010ns+ | Flexible | Variable | Process write operation |

**Synchronous Sampling**: The MIA samples signals at precise times relative to the clock:
- **Address at 60ns** (after PHI2 low + settling time)
- **CS at 200ns** (after address mapping logic)
- **R/W and OE at 530ns** (after PHI2 high + propagation delay)

During the 200-530ns speculative preparation window, the MIA doesn't know if the operation will be a read or write. It can speculatively fetch data (assuming read), but must be prepared to discard this and switch to write mode when R/W is sampled at 530ns.

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
│ MIA Synchronous Bus Control State Machine                  │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  DEFAULT STATE: Wait for PHI2 low                          │
│    └─ D0-D7 configured as inputs (tri-stated)              │
│                                                            │
│  At PHI2 falling edge (0ns):                               │
│    └─ Detect clock low, start cycle                        │
│                                                            │
│  At 60ns (address sample time):                            │
│    ├─ Sample address bus A0-A7                             │
│    └─ Begin address decode                                 │
│                                                            │
│  At 200ns (CS sample time):                                │
│    ├─ Sample CS signal                                     │
│    ├─ IF CS = LOW (selected):                              │
│    │    ├─ Complete register decode                        │
│    │    └─ Prepare data (speculative, assuming read)       │
│    └─ IF CS = HIGH (not selected):                         │
│         └─ Return to wait for PHI2 low                     │
│                                                            │
│  At PHI2 rising edge (500ns):                              │
│    └─ Detect clock high                                    │
│                                                            │
│  At 530ns (R/W sample time):                               │
│    ├─ Sample R/W and OE signals                            │
│    ├─ IF R/W = HIGH (READ):                                │
│    │    ├─ Verify OE = LOW                                 │
│    │    ├─ Configure D0-D7 as outputs                      │
│    │    └─ Drive prepared data                             │
│    │                                                       │
│    └─ IF R/W = LOW (WRITE):                                │
│         ├─ Verify OE = HIGH                                │
│         ├─ Keep D0-D7 as inputs                            │
│         └─ Prepare to sample data                          │
│                                                            │
│  At PHI2 falling edge (1000ns):                            │
│    ├─ Detect clock low                                     │
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
│  At 1015ns (cycle complete):                               │
│    └─ Return to wait for PHI2 low                          │
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
- Synchronous operation with generated clock
- No CPU intervention for bus cycles
- Can handle back-to-back cycles reliably
- Precise timing control for signal sampling

**PIO State Machine Logic (Synchronous)**:
```
1. WAIT for PHI2 = LOW (clock falling edge)
2. DELAY 60ns (7-8 PIO instructions at 125 MHz)
3. IN pins (sample A0-A7 address) → push to RX FIFO
4. DELAY to 200ns (additional ~17 instructions)
5. IN pins (sample CS)
6. JMP if CS = HIGH (not selected) → back to step 1
7. WAIT for PHI2 = HIGH (clock rising edge)
8. DELAY 30ns (3-4 PIO instructions)
9. IN pins (sample OE pin)
10. JMP if OE = HIGH (not enabled) → back to step 1
11. IN pins (sample R/W pin)
12. JMP if R/W = LOW (write operation) → goto WRITE_HANDLER
13. READ_HANDLER:
    - Pull data from TX FIFO (CPU prepared based on address)
    - SET PINDIRS (configure D0-D7 as outputs)
    - OUT pins (drive data onto D0-D7)
    - WAIT for PHI2 = LOW (falling edge at 1000ns)
    - DELAY 15ns (2 instructions for tDHR)
    - SET PINDIRS (tri-state D0-D7)
    - JMP to step 1
14. WRITE_HANDLER:
    - WAIT for PHI2 = LOW (falling edge at 1000ns)
    - IN pins (latch D0-D7 data) → push to RX FIFO
    - JMP to step 1
```

**Note on Address Decode**: The address sampled in step 3 is pushed to the RX FIFO. The CPU core reads this FIFO, decodes the address, fetches the appropriate data, and places it in the TX FIFO before the PIO needs it at step 13. This happens during the 200-530ns window while PIO is waiting.

**PIO Jump Limitations**: PIO can only jump based on a single pin state. The logic above uses separate jumps for CS (step 6), OE (step 10), and R/W (step 12). Each jump tests one pin and branches accordingly.

**OE Protection**: Step 10 explicitly checks OE before proceeding. If OE is HIGH (not asserted), the PIO jumps back to wait for the next cycle, ensuring the MIA never drives the bus when OE is not active.

**Clock Generation**:
- MIA generates PHI2 clock via PWM
- PIO can read clock state directly from GPIO or PWM peripheral
- Synchronous design eliminates race conditions

**Data Preparation**:
- CPU reads address from RX FIFO during 200-530ns window
- CPU decodes address and fetches data
- CPU places response data in TX FIFO
- PIO pulls data from TX FIFO when needed (step 13)
- Timing budget: ~330ns for CPU to prepare data

#### Option 2: Synchronous Polling Loop

Use CPU polling loop synchronized with generated clock:

**Advantages**:
- Simpler to program than PIO
- Direct control over timing
- Easier debugging
- Can read clock state directly

**Challenges**:
- CPU must be dedicated to bus interface
- Requires precise timing control
- More CPU overhead than PIO

**Synchronous Handler**:
```c
void synchronous_bus_handler() {
    while (1) {
        // Wait for PHI2 low (clock falling edge)
        while (PHI2_HIGH) { /* spin */ }
        
        // Wait 60ns for address to settle
        delay_ns(60);
        
        // Sample address bus
        uint8_t addr = read_address_bus();
        
        // Wait until 200ns mark
        delay_ns(140);
        
        // Sample CS
        if (CS_HIGH) {
            continue; // Not selected, wait for next cycle
        }
        
        // Decode and fetch data (~100-200ns available)
        uint8_t data = decode_and_fetch(addr);
        
        // Wait for PHI2 high (clock rising edge)
        while (PHI2_LOW) { /* spin */ }
        
        // Wait 30ns for R/W to settle
        delay_ns(30);
        
        // Sample R/W and OE
        bool is_read = (RW_HIGH);
        
        if (is_read) {
            // Drive data for read
            drive_data_bus(data);
            
            // Wait for PHI2 falling edge
            while (PHI2_HIGH) { /* spin */ }
            
            // Hold data for tDHR (15ns)
            delay_ns(15);
            
            // Tri-state
            tristate_data_bus();
        } else {
            // Wait for PHI2 falling edge to latch write data
            while (PHI2_HIGH) { /* spin */ }
            
            // Latch data on falling edge
            uint8_t write_data = read_data_bus();
            
            // Process write
            process_write(addr, write_data);
        }
    }
}
```

#### Option 3: Hybrid PIO + CPU (Best Approach)

**Best of both worlds**:
- PIO handles synchronous bus protocol and precise timing
- PIO samples signals at exact moments (60ns, 200ns, 530ns)
- CPU handles data preparation and processing
- PIO and CPU communicate via FIFO
- Clock generation integrated with PIO timing

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
| PHI2 | TBD | Output/Input | Clock phase 2 - **MIA GENERATES** (can read back for sync) |
| IRQ | 26 | Output | Interrupt request to CPU |

**Note**: R/W signal connection is **required** but not currently in requirements. PHI2 is **generated by MIA** and output to the 6502. The MIA can read the PHI2 state directly from the PWM peripheral or GPIO pin for synchronous operation.

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

1. **MIA operates synchronously** - Generates clock and uses it to precisely time all signal sampling

2. **Avoids transient signals** - Samples address at 60ns, CS at 200ns, R/W at 530ns (after settling)

3. **At 1 MHz, timing is very comfortable** - MIA has 785ns to prepare read data and 470ns to sample write data

4. **OE signal is critical** - It prevents bus contention by controlling when MIA drives the bus

5. **R/W signal must be connected** - Currently missing from requirements, needed to distinguish read from write

6. **Speculative preparation is possible** - MIA can start work at CS assertion (200ns) before knowing operation type (530ns)

7. **PIO implementation recommended** - Provides deterministic timing and minimal CPU overhead for synchronous operation

8. **All timing margins are adequate** - No critical timing paths at 1 MHz operation

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
