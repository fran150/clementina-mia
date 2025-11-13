# MIA System Integration Guide

Complete guide for integrating the MIA (Multifunction Interface Adapter) into your 6502 hardware system.

## Table of Contents

1. [Hardware Connections](#hardware-connections)
2. [Boot Process](#boot-process)
3. [Clock Configuration](#clock-configuration)
4. [Memory Map](#memory-map)
5. [Kernel Development](#kernel-development)
6. [System Initialization](#system-initialization)

---

## Hardware Connections

### Required Signals

| Signal | GPIO | Direction | Description |
|--------|------|-----------|-------------|
| A0-A7 | 0-7 | Input | Address bus (low 8 bits) |
| D0-D7 | 8-15 | Bidirectional | Data bus |
| PICOHIRAM | 16 | Output | Bank MIA in/out of high memory |
| Reset | 17 | Output | Reset 6502 CPU |
| WE | 18 | Input | Write Enable (active low) |
| OE | 19 | Input | Output Enable (active low) |
| HIRAM_CS | 20 | Input | High RAM chip select (active low) |
| IO0_CS | 21 | Input | I/O chip select (active low) |
| IRQ | 26 | Output | Interrupt request to CPU |

### Address Decoding

The MIA responds to two address ranges:

**I/O Space ($C000-$C3FF):**
- Decoded by `IO0_CS` signal (active low)
- Used for indexed memory interface
- Available in both boot and normal phases

**High Memory ($E000-$FFFF):**
- Decoded by `HIRAM_CS` signal (active low)
- Used for ROM emulation during boot
- MIA automatically banks out after kernel loading

### Typical Address Decoder

```
A15 A14 A13 A12 | Range        | Signal
----------------|--------------|--------
 1   1   0   0  | $C000-$CFFF  | IO0_CS (active low)
 1   1   1   x  | $E000-$FFFF  | HIRAM_CS (active low)
```

### Power and Ground

- Connect Pico 2 W VSYS to 5V power supply
- Connect all GND pins
- Ensure common ground with 6502 system

---

## Boot Process

The MIA provides a sophisticated boot sequence that loads your kernel code and transitions to full-speed operation.

### Boot Sequence Overview

```
1. Power On
   ↓
2. MIA Initialization (100 kHz clock)
   ↓
3. 6502 Reset Released
   ↓
4. 6502 Reads Reset Vector ($FFFC-$FFFD)
   ↓
5. Boot Loader Executes ($E000)
   ↓
6. Kernel Loading (via memory-mapped I/O)
   ↓
7. MIA Auto-Transition (banks out, 1 MHz clock)
   ↓
8. Kernel Starts ($4000)
```

### Phase 1: Boot Phase (100 kHz)

**What happens:**
- MIA starts at 100 kHz for reliable boot
- ROM emulation active at $E000-$FFFF
- Boot loader code provided by MIA
- Kernel data streamed via memory-mapped registers

**Boot Loader Memory Map:**
```
$E000-$E0FF: Boot loader code
$E100:       Status register (1=more data, 0=complete)
$E101:       Data register (next kernel byte)
$FFFC-$FFFD: Reset vector → $E000
```

**Boot Loader Operation:**
```assembly
; Simplified boot loader (provided by MIA)
BOOT_START:
    LDX #$00               ; Destination offset
    LDY #$40               ; Destination page ($4000)
    
.load_loop:
    LDA $E100              ; Check status
    BEQ .load_done         ; 0 = no more data
    
    LDA $E101              ; Read kernel byte
    STA ($00),Y            ; Store at $4000+
    
    INX
    BNE .load_loop
    INY
    JMP .load_loop
    
.load_done:
    JMP $4000              ; Start kernel
```

### Phase 2: Auto-Transition

**MIA automatically detects kernel loading completion and:**
1. Banks out of high memory ($E000-$FFFF)
2. Increases clock from 100 kHz to 1 MHz
3. Signals completion via status register

**No manual intervention required!**

### Phase 3: Normal Operation (1 MHz)

**What happens:**
- Full 1 MHz operation
- ROM emulation disabled
- All MIA features available
- Your kernel has full control

---

## Clock Configuration

### Boot Clock: 100 kHz

**Why 100 kHz?**
- Ensures reliable boot across all hardware
- Allows C-based ROM emulation to meet timing
- Provides stable environment for kernel loading

**Timing at 100 kHz:**
- Cycle time: 10,000ns (10µs)
- Plenty of time for MIA to respond
- Very forgiving for initial setup

### Normal Clock: 1 MHz

**Why 1 MHz?**
- Good balance of speed and reliability
- MIA can meet all timing requirements
- Standard 6502 operating frequency

**Timing at 1 MHz:**
- Cycle time: 1,000ns (1µs)
- MIA response time: <785ns (comfortable margin)
- See `bus_timing.md` for detailed specifications

### Clock Duty Cycle

- Approximately 50% duty cycle
- 500ns high, 500ns low at 1 MHz
- Generated via PWM for accuracy

---

## Memory Map

### Complete Address Space

```
$0000-$BFFF: External RAM/ROM (not managed by MIA)
$C000-$C3FF: MIA I/O Space (indexed memory interface)
$C400-$DFFF: External RAM/ROM (not managed by MIA)
$E000-$FFFF: Boot Phase: MIA ROM emulation
             Normal Phase: External RAM/ROM
```

### MIA I/O Space Detail ($C000-$C3FF)

```
$C000-$C03F: Windows A-D (4 windows × 16 registers)
  $C000-$C00F: Window A
  $C010-$C01F: Window B
  $C020-$C02F: Window C
  $C030-$C03F: Window D

$C040-$C07F: Reserved for future windows (E-H)

$C080-$C0FF: Shared registers
  $C0F0: DEVICE_STATUS
  $C0F1: IRQ_CAUSE_LOW
  $C0F2: IRQ_CAUSE_HIGH
  $C0F3: IRQ_MASK_LOW
  $C0F4: IRQ_MASK_HIGH
  $C0F5: IRQ_ENABLE
  $C0F6-$C0FF: Device identification

$C100-$C3FF: Mirror of $C000-$C0FF (3 times)
```

### Boot Phase ROM Space ($E000-$FFFF)

```
$E000-$E0FF: Boot loader code (provided by MIA)
$E100:       Status register (1=more data, 0=complete)
$E101:       Data register (returns next kernel byte)
$E102-$FFFB: Mirrored (256-byte pattern repeats)
$FFFC-$FFFD: Reset vector → $E000
$FFFE-$FFFF: IRQ vector (if used)
```

---

## Kernel Development

### Overview

The MIA supports loading kernel code from an external `kernel.bin` file at compile time. This allows independent kernel development without modifying MIA firmware.

### Kernel Requirements

Your kernel should:
- Start execution at address `$4000`
- Initialize the 6502 system (stack, interrupts, etc.)
- Be aware that MIA has already transitioned to 1 MHz

### Development Workflow

**1. Develop Your Kernel**

Create your 6502 kernel code:

```assembly
; Kernel entry point at $4000
.org $4000

KERNEL_START:
    SEI                     ; Disable interrupts
    CLD                     ; Clear decimal mode
    
    ; Initialize stack
    LDX #$FF
    TXS
    
    ; MIA has already:
    ; - Banked out of high memory
    ; - Increased clock to 1 MHz
    ; - Your kernel initialization here
    
    ; Initialize MIA interface
    JSR init_mia
    
    ; Main kernel loop
MAIN_LOOP:
    ; Your main kernel code
    JMP MAIN_LOOP

init_mia:
    ; Configure indexes, set up interrupts, etc.
    RTS
```

**2. Generate kernel.bin**

Compile/assemble your kernel to produce `kernel.bin`:

```bash
# Example with ca65/ld65
ca65 kernel.asm -o kernel.o
ld65 kernel.o -o kernel.bin -C kernel.cfg
```

**3. Place kernel.bin**

Copy `kernel.bin` to the MIA project root directory (same directory as `CMakeLists.txt`).

**4. Build MIA Firmware**

```bash
make build
```

The build system will:
- Detect `kernel.bin`
- Convert it to C array
- Embed in MIA firmware
- Generate `mia.uf2`

**5. Upload to Pico**

Copy `mia.uf2` to your Pico 2 W (drag and drop in bootloader mode).

### Missing kernel.bin

If `kernel.bin` is not present:
- Build continues with empty kernel
- Warning message displayed
- MIA functions but has no kernel to load

### Kernel Size Limits

- Maximum kernel size: Limited by available MIA memory
- Loaded starting at $4000
- Can use memory up to MIA I/O space ($C000)

---

## System Initialization

### Minimal 6502 System

**Required components:**
1. W65C02S CPU
2. MIA (Raspberry Pi Pico 2 W)
3. Address decoder (for IO0_CS and HIRAM_CS)
4. Power supply (5V)

**Optional components:**
- External RAM (for $0000-$BFFF)
- External ROM (for normal phase $E000-$FFFF)
- Additional I/O devices

### Power-On Sequence

1. **Apply power** to Pico 2 W and 6502 system
2. **MIA initializes** (takes ~100ms)
3. **MIA asserts Reset** (holds 6502 in reset)
4. **MIA starts clock** at 100 kHz
5. **MIA releases Reset** (6502 begins execution)
6. **Boot loader runs** (loads kernel)
7. **MIA transitions** (1 MHz, banks out)
8. **Kernel starts** at $4000

### First Boot Checklist

- [ ] All power connections secure
- [ ] Common ground between Pico and 6502
- [ ] Address decoder working (test with logic analyzer)
- [ ] Data bus connections correct
- [ ] Control signals (WE, OE, CS) connected
- [ ] Reset and IRQ lines connected
- [ ] kernel.bin present in MIA project
- [ ] MIA firmware built and uploaded

### Troubleshooting

**System doesn't boot:**
- Check Reset line (should pulse low then high)
- Verify clock signal (should be 100 kHz initially)
- Check address decoder (IO0_CS and HIRAM_CS)
- Verify power supply voltage (5V)

**Kernel doesn't load:**
- Check that kernel.bin exists
- Verify kernel starts at $4000
- Check MIA debug output (USB serial)
- Verify boot loader can read from $E100/$E101

**System hangs after boot:**
- Check that kernel initializes stack
- Verify interrupt vectors if using IRQ
- Check for infinite loops in kernel
- Verify MIA I/O space is accessible

---

## Critical Timing Requirements

### Bus Timing (1 MHz Operation)

From `bus_timing.md`:

**Read Cycle:**
- Address valid: 40ns after PHI2 falls
- CS valid: 200ns after PHI2 falls
- Data must be valid: 985ns (15ns before PHI2 falls)
- MIA has 785ns to respond (comfortable margin)

**Write Cycle:**
- Address valid: 40ns after PHI2 falls
- CS valid: 200ns after PHI2 falls
- Data valid: 540ns after PHI2 rises
- Data hold: 10ns after PHI2 falls

**MIA Response:**
- Read response: <785ns (always met)
- Write processing: Immediate
- DMA operations: Non-blocking (never delays bus)

### Signal Timing

**Reset:**
- Minimum pulse width: 2 clock cycles
- MIA holds reset for ~100ms during initialization

**IRQ:**
- Asserted when interrupt pending
- Cleared when interrupt acknowledged
- Edge-triggered (falling edge)

---

## Advanced Topics

### Multiple MIA Devices

You can use multiple MIA devices in one system:
- Each MIA has unique I/O address range
- Separate CS signals for each MIA
- Shared address/data buses
- Independent clock generation

### Custom Boot Loaders

While MIA provides a boot loader, you can:
- Modify the boot loader in MIA firmware
- Use external ROM for boot instead
- Implement custom kernel loading schemes

### Performance Optimization

**For maximum performance:**
- Use DMA for large memory copies
- Configure indexes once, reuse many times
- Use interrupts instead of polling
- Group related operations together

---

## Quick Reference

### GPIO Pin Summary

| Pin | Signal | Direction | Description |
|-----|--------|-----------|-------------|
| 0-7 | A0-A7 | In | Address bus |
| 8-15 | D0-D7 | Bidir | Data bus |
| 16 | PICOHIRAM | Out | Bank control |
| 17 | Reset | Out | CPU reset |
| 18 | WE | In | Write enable |
| 19 | OE | In | Output enable |
| 20 | HIRAM_CS | In | High RAM CS |
| 21 | IO0_CS | In | I/O CS |
| 26 | IRQ | Out | Interrupt |

### Boot Phase Registers

| Address | Register | Description |
|---------|----------|-------------|
| $E100 | STATUS | 1=more data, 0=complete |
| $E101 | DATA | Next kernel byte |
| $FFFC-$FFFD | RESET_VECTOR | Points to $E000 |

### Clock Frequencies

| Phase | Frequency | Purpose |
|-------|-----------|---------|
| Boot | 100 kHz | Reliable boot, kernel loading |
| Normal | 1 MHz | Full-speed operation |

---

For programming the MIA from 6502 code, see `mia_programming_guide.md`.  
For detailed timing specifications, see `bus_timing.md`.  
For complete API reference, see `mia_indexed_interface_reference.md`.
