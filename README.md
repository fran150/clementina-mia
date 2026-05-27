# MIA (Multifunction Interface Adapter)

MIA is the Raspberry Pi Pico 2 W based interface adapter for the Clementina 6502 computer. It appears to the 6502 as a 32-byte register block at `$FFE0-$FFFF`, generates the 6502 `PHI2` clock, handles the data bus with PIO/DMA, provides 128 KiB of internal RAM behind indexed windows, and boots Clementina by exposing a small loader through the reset vector.

The firmware is built as a `copy_to_ram` Pico application so the time-critical PIO/DMA register path can run without flash stalls.

## Hardware role

- Emulates the top 32 bytes of the 6502 address space as MIA registers.
- Drives `PHI2` from PIO. The default rate is 2 kHz and can be changed through configuration registers.
- Drives active-low `IRQB` when enabled interrupt flags are pending.
- Drives active-low `RESB` during startup and while the external MIA reset request line is asserted.
- Uses PIO state machines and DMA to read and write register bytes fast enough for bus cycles.
- Provides two CPU-facing indexed memory windows, `IDX A` and `IDX B`, backed by MIA RAM.

## Boot and runtime modes

MIA starts in loader mode. During initialization it writes a tiny 6502 program into the register block and points the reset vector at `$FFE0`. The loader streams `kernel.bin`, embedded at build time as `kernel_data`, into Clementina RAM starting at `$4000`. Once the embedded kernel bytes are consumed, MIA switches to normal mode and sets `MIA_STAT_MASTER_MODE`.

In normal mode, register reads and writes operate as the interface described below. The main loop also services requested `PHI2` speed changes and reset requests.

## Register Map

MIA exposes 32 internal registers. The 6502 sees them at `$FFE0-$FFFF`; internally only the low 5 address bits are used.

| Register | Clementina | Name | Description |
|----------|------------|------|-------------|
| `00` | `$FFE0` | `IDXA_PORT` | Data port for the index selected by `IDXA_SELECT`. Reads return the current RAM byte, then optionally step index A and preload the next byte. Writes store the byte to RAM, then optionally step index A and refresh the port. |
| `01` | `$FFE1` | `IDXA_SELECT` | Selects which of the 256 index descriptors is attached to index window A. Writing a selector also preloads `IDXA_PORT` from that index's current address. |
| `02` | `$FFE2` | `CFG_SELECT` | Selects a configuration register. Writing this register loads the selected config value into `CFG_PORT`. |
| `03` | `$FFE3` | `CFG_PORT` | Configuration data port. After selecting a config id, this register contains the current config value. Writes to this register update the selected config entry. |
| `04` | `$FFE4` | `IDXB_PORT` | Data port for the index selected by `IDXB_SELECT`. Behaves like `IDXA_PORT`, but uses index window B and B-specific wrap IRQs. |
| `05` | `$FFE5` | `IDXB_SELECT` | Selects which index descriptor is attached to index window B. Writing a selector also preloads `IDXB_PORT` from that index's current address. |
| `06` | `$FFE6` | `CMD_PARAM1` | First command parameter. Latched into the command FIFO message when `CMD_TRIGGER` is written. |
| `07` | `$FFE7` | `CMD_PARAM2` | Second command parameter. Latched into the command FIFO message when `CMD_TRIGGER` is written. |
| `08` | `$FFE8` | `CMD_PARAM3` | Third command parameter. Latched into the command FIFO message when `CMD_TRIGGER` is written. |
| `09` | `$FFE9` | `CMD_TRIGGER` | Command id. Writing queues `[command id, param1, param2, param3]` to core 0's command handler if the multicore FIFO is ready. |
| `0A` | `$FFEA` | `STATUS_L` | Low byte of the 16-bit status register. |
| `0B` | `$FFEB` | `STATUS_H` | High byte of the 16-bit status register. |
| `0C` | `$FFEC` | `ERROR_L` | Error queue read port. Reading this address pulls the next queued error into `ERROR_L`; zero means no error. |
| `0D` | `$FFED` | `ERROR_H` | High byte of the error register. Currently unused by the error queue. |
| `0E` | `$FFEE` | `IRQ_MASK_L` | Low byte of the IRQ mask. After writes, MIA re-evaluates the IRQ output. |
| `0F` | `$FFEF` | `IRQ_MASK_H` | High byte of the IRQ mask. After writes, MIA re-evaluates the IRQ output. |
| `10` | `$FFF0` | `IRQ_STATUS_L` | Low byte of pending IRQ flags. The 6502 can write status bits, then MIA re-evaluates the IRQ output. |
| `11` | `$FFF1` | `IRQ_STATUS_H` | High byte of pending IRQ flags. Bit 15 is maintained as the aggregate IRQ-triggered state. |
| `12-19` | `$FFF2-$FFF9` | `RESERVED` | Reserved register bytes. |
| `1A` | `$FFFA` | `NMI_VECTOR_L` | Low byte of the 6502 NMI vector exposed by MIA. |
| `1B` | `$FFFB` | `NMI_VECTOR_H` | High byte of the 6502 NMI vector exposed by MIA. |
| `1C` | `$FFFC` | `RESET_VECTOR_L` | Low byte of the 6502 reset vector. Loader mode initializes this to `$FFE0`. |
| `1D` | `$FFFD` | `RESET_VECTOR_H` | High byte of the 6502 reset vector. Loader mode initializes this to `$FFE0`. |
| `1E` | `$FFFE` | `IRQ_BRK_VECTOR_L` | Low byte of the 6502 IRQ/BRK vector exposed by MIA. |
| `1F` | `$FFFF` | `IRQ_BRK_VECTOR_H` | High byte of the 6502 IRQ/BRK vector exposed by MIA. |

## Indexed RAM

MIA reserves 128 KiB of RAM. It is accessed through 256 index descriptors, each 16 bytes wide:

| Field | Size | Description |
|-------|------|-------------|
| `current_addr` | 24 bits used | Current MIA RAM address for the index. Actual RAM access is masked to 128 KiB. |
| `default_addr` | 24 bits used | Address restored by reset-index commands and used as the forward wrap target. |
| `limit_addr` | 24 bits used | Exclusive upper limit for forward wrapping; backward wrapping jumps to `limit_addr - 1`. |
| `step` | 16 bits | Unsigned step magnitude. Direction comes from the index flags. |
| `flags` | 8 bits | Controls read/write stepping, direction, wrapping, and wrap IRQs. |
| `reserved` | 8 bits | Padding/reserved. |

Only two index descriptors are active on the CPU bus at a time: window A selected by `IDXA_SELECT`, and window B selected by `IDXB_SELECT`.

## Configuration Registers

The config interface uses `CFG_SELECT` and `CFG_PORT`. Write a config id to `$FFE2` (`CFG_SELECT`) to load its current value into `$FFE3` (`CFG_PORT`); read or write `$FFE3` to access the selected config id.

Config ids `$00-$1F` configure index descriptors 0 and 1 directly. The high nibble selects the index id (`0` for index 0, `1` for index 1) and the low nibble selects the field. Higher index descriptors are still usable by the index windows and commands, but this config window currently only maps indexes 0 and 1.

| # | CFG Index | Description |
|---|-----------|-------------|
| `00` | `IDX0_ADDR_L` | Low byte of index 0 current address. |
| `01` | `IDX0_ADDR_M` | Middle byte of index 0 current address. |
| `02` | `IDX0_ADDR_H` | High byte of index 0 current address. |
| `03` | `IDX0_DEF_L` | Low byte of index 0 default address. |
| `04` | `IDX0_DEF_M` | Middle byte of index 0 default address. |
| `05` | `IDX0_DEF_H` | High byte of index 0 default address. |
| `06` | `IDX0_LIM_L` | Low byte of index 0 limit address. |
| `07` | `IDX0_LIM_M` | Middle byte of index 0 limit address. |
| `08` | `IDX0_LIM_H` | High byte of index 0 limit address. |
| `09` | `IDX0_STP_L` | Low byte of index 0 step magnitude. |
| `0A` | `IDX0_STP_H` | High byte of index 0 step magnitude. |
| `0B` | `IDX0_FLAGS` | Index 0 flags. |
| `0C-0F` | Reserved | Reads as zero; writes are ignored. |
| `10` | `IDX1_ADDR_L` | Low byte of index 1 current address. |
| `11` | `IDX1_ADDR_M` | Middle byte of index 1 current address. |
| `12` | `IDX1_ADDR_H` | High byte of index 1 current address. |
| `13` | `IDX1_DEF_L` | Low byte of index 1 default address. |
| `14` | `IDX1_DEF_M` | Middle byte of index 1 default address. |
| `15` | `IDX1_DEF_H` | High byte of index 1 default address. |
| `16` | `IDX1_LIM_L` | Low byte of index 1 limit address. |
| `17` | `IDX1_LIM_M` | Middle byte of index 1 limit address. |
| `18` | `IDX1_LIM_H` | High byte of index 1 limit address. |
| `19` | `IDX1_STP_L` | Low byte of index 1 step magnitude. |
| `1A` | `IDX1_STP_H` | High byte of index 1 step magnitude. |
| `1B` | `IDX1_FLAGS` | Index 1 flags. |
| `1C-1F` | Reserved | Reads as zero; writes are ignored. |
| `20` | `SPEED_L` | Low byte of applied/requested `PHI2` frequency in Hz. |
| `21` | `SPEED_M` | Middle byte of applied/requested `PHI2` frequency in Hz. |
| `22` | `SPEED_H` | High byte of applied/requested `PHI2` frequency in Hz. Writing this byte commits the staged speed change. |

## Index Flags

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `R_STP_ENA` | Step the active index after `IDX*_PORT` is read. |
| 1 | `W_STP_ENA` | Step the active index after `IDX*_PORT` is written. |
| 2 | `STP_DIR` | Step direction: `0` = forward, `1` = backward. |
| 3 | `WRAP_ENA` | Enable wrapping when the current address crosses the configured range. |
| 4 | `WRAP_IRQ` | Raise the window-specific wrap IRQ flag when wrapping occurs. |

Forward wrapping occurs when `current_addr >= limit_addr` and resets `current_addr` to `default_addr`. Backward wrapping occurs when `current_addr < default_addr` and resets `current_addr` to `limit_addr - 1`.

## Commands

Commands are requested by writing parameters to `CMD_PARAM1-3`, then writing the command id to `CMD_TRIGGER`. The command handler runs from the multicore FIFO path and sets `MIA_STAT_CMD_RUNNING` while draining queued commands.

| Command | Parameters | Description |
|---------|------------|-------------|
| `00` | none | Reset the index selected in window A to its default address. |
| `01` | none | Reset the index selected in window B to its default address. |
| `02` | `p1 = index id` | Reset the specified index to its default address. |
| `03` | `p1 = index id` | Copy the specified index current address to its default address. |
| `04` | `p1 = index id` | Copy the specified index current address to its limit address. |
| `05` | none | Reset all 256 indexes to their default addresses. |
| `06` | `p1 = index id` | Peek the specified index's current RAM byte into `IDXA_PORT` without stepping. |
| `07` | `p1 = index id` | Peek the specified index's current RAM byte into `IDXB_PORT` without stepping. |
| `10` | `p1 = source index`, `p2 = destination index`, `p3 = byte count` | Start a DMA copy inside MIA RAM. Source and destination indexes are not moved. |

Unassigned command ids are no-ops.

## IRQ Status

`IRQ_STATUS & IRQ_MASK` controls the physical active-low `IRQB` line. If any enabled flag is set, MIA also sets bit 15 (`IRQ_TRIGGERED`) and drives `IRQB` low. If no enabled flags are pending, bit 15 is cleared and `IRQB` is released high.

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `IRQ_ERROR` | An error was pushed into the error queue. |
| 1 | `IRQ_IDXA_WRAPPED` | The active index in window A wrapped and its `WRAP_IRQ` flag was enabled. |
| 2 | `IRQ_IDXB_WRAPPED` | The active index in window B wrapped and its `WRAP_IRQ` flag was enabled. |
| 3 | `IRQ_COMMAND` | Reserved for command-triggered interrupts. |
| 4 | `IRQ_SPEED_CHANGED` | A requested `PHI2` speed change was applied. |
| 15 | `IRQ_TRIGGERED` | Aggregate state maintained by MIA when any masked IRQ flag is pending. |

## Status

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `MIA_STAT_MASTER_MODE` | `0` = loader mode, `1` = normal mode. |
| 1 | `MIA_STAT_ERRORS` | Error queue contains at least one error. |
| 2 | `MIA_STAT_CMD_RUNNING` | Command handler is executing queued commands. |
| 3 | `MIA_STAT_DMA_RUNNING` | MIA RAM DMA copy is in progress. |
| 4 | `MIA_STAT_SPEED_CHANGING` | A `PHI2` speed change has been requested and not yet applied. |

## Errors

Errors are stored in a 16-entry ring buffer. Reading `$FFEC` pulls one error into `ERROR_L`; when the queue becomes empty, `MIA_STAT_ERRORS` is cleared.

| Code | Name | Description |
|------|------|-------------|
| `01` | `ERROR_MIA_CANNOT_ALLOCATE_RAM` | Reserved/startup RAM allocation failure code. Current RAM is statically allocated, so this should not normally occur. |
| `10` | `ERROR_DMA_SIZE_ZERO` | DMA copy requested with a byte count of zero. |
| `11` | `ERROR_DMA_SRC_WILL_OVERFLOW` | DMA source range would exceed the 128 KiB MIA RAM region. |
| `12` | `ERROR_DMA_TGT_WILL_OVERFLOW` | DMA destination range would exceed the 128 KiB MIA RAM region. |

## PHI2 Speed Control

The `SPEED_L/M/H` config registers hold a 24-bit `PHI2` frequency in Hz. Writes are staged byte by byte; writing `SPEED_H` commits the request. `mia_service()` later clamps the requested value to the achievable PIO divider range, applies the new divider to the write/read/action state machines, clears `MIA_STAT_SPEED_CHANGING`, and raises `IRQ_SPEED_CHANGED`.

## GPIO Mapping

| GPIO | Signal | Direction | Description |
|------|--------|-----------|-------------|
| 6 | `MIA_CS` | Input | Chip select sampled by PIO. |
| 7 | `MIA_RWB` | Input | 6502 read/write line sampled by PIO. |
| 8-15 | `D0-D7` | Bidirectional | 6502 data bus. Direction is controlled by the CS/RWB PIO program. |
| 16-20 | `A0-A4` | Input | Low address bits used to select one of the 32 MIA registers. |
| 21 | `CPU_PHI2` | Output | 6502 `PHI2` clock generated by the write PIO program. |
| 22 | `CPU_IRQB` | Output | Active-low IRQ output to the 6502. |
| 26 | `CPU_RESB` | Output | Active-low reset output to the 6502. |
| 27 | `MIA_RESETB` | Input | Active-low reset request into MIA. |

## Build and Flash

The build expects the Pico SDK and uses `kernel.bin` from the repository root. CMake converts that binary into `kernel_data.c` during the build.

```sh
make build
make flash
```

USB stdio is enabled and UART stdio is disabled. During the initial debug loop, sending `c` over USB continues into MIA initialization, and sending `q` reboots the Pico into BOOTSEL.
