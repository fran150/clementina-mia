# Diagnostics

These console commands look inside MIA while a program runs. They answer
questions such as "which index was selected when this went wrong" or "what did
the 6502 write to MIA just before the error" without a special build or a logic
analyzer.

## They do not touch the bus

Core 1 runs the action loop that answers the 6502's register accesses. The
console runs on core 0, and every command here runs there. It only reads what
core 1 and the PIOs already keep, so core 1 has nothing extra to do. The
action loop in the normal firmware is the same instruction for instruction as
before these commands existed.

There are two things to keep in mind:

- **A copy can be torn.** Core 1 keeps changing registers, indexes and RAM
  while the 6502 runs, and the console reads them without stopping it. For an
  exact copy, run `exec pause` first (it stops `PHI2`), dump, then
  `exec resume`. The file dumps remind you when the 6502 is running.
- **A long dump keeps core 0 busy.** Wi-Fi, video, input and the 6502's SD
  requests wait while a big dump prints on the console. Write big dumps to a
  file instead. During a file write, card waits still serve Wi-Fi, video and
  input, as they do for the 6502's own SD requests.

## Commands

| Command | What it prints |
| ------- | -------------- |
| `status ctx` | The `MIA_CTX` stack: its depth, and for each saved entry the selectors, command parameters and the two saved index descriptors. |
| `status pio` | For the bus PIOs (`pio0`: SM0 CS/RWB, SM1 write and `PHI2`, SM2 read; `pio1`: SM0 action), the `FDEBUG` flags, each state machine's FIFO levels and program counter. |
| `status pio clear` | Reset the sticky `FDEBUG` flags. |
| `dump regs` | The 32 registers `$FFE0-$FFFF` with their names. |
| `dump idx` | Every index descriptor with any non-zero field. |
| `dump idx ID` | One descriptor, ID in decimal or `$` hex. |
| `dump ram ADDR LEN` | LEN bytes of MIA RAM from ADDR, as hex and ASCII, as the monitor's `m` does. ADDR and LEN are hex. |
| `dump ram ADDR LEN FILE` | The same bytes, raw, into FILE on the SD card. |
| `dump all` | Registers, `MIA_CTX`, PIO flags, error queue and the indexes in use (and the trace, in the diagnostic build). |
| `dump all FILE` | A [snapshot](#snapshot-file) of everything, all of MIA RAM included, into FILE. |
| `trace` / `trace clear` | The [bus event trace](#bus-event-trace) (diagnostic build only). |

`RXSTALL` on the action state machine (`pio1` SM0) means core 1 fell behind
and the PIO dropped bus events. `TXSTALL` on the write state machine is
normal: it waits for data between cycles.

### Files on the SD card

FILE is a path, relative to the current directory the 6502 last set with
`FS_CHDIR` (the root after boot). An existing file is replaced. If the 6502 has
not mounted the card, the command mounts it for the write and unmounts it
again, so the 6502 still finds the filesystem as it left it. While the 6502 has
an SD request or a load or save running, the command refuses with
`SD busy`; run it again. A snapshot is about 260 KB.

## Snapshot file

`dump all FILE` writes a binary file. All numbers are little-endian. It starts
with the 7 bytes `MIASNAP` and a format version byte (1), followed by
sections. Each section has a 4-character tag, a `u32` length, then the data.
The last section is `END ` with length 0. A reader skips tags it does not know.

| Tag | Length | Contents |
| --- | ------ | -------- |
| `INFO` | 16 | `u32` applied `PHI2` Hz, `u16` status word, `u8` flags (bit 0 normal mode, bit 1 exec paused, bit 2 diagnostic build), `u8` reserved, `u32` milliseconds since MIA booted, `u32` RAM size. |
| `REGS` | 32 | The registers, `$FFE0` first. |
| `IDX ` | 4096 | 256 index descriptors of 16 bytes: `u32` current, `u32` default, `u32` limit (24-bit addresses), `u16` step, `u8` flags, `u8` reserved. |
| `CTX ` | 1 + 38 per entry | `u8` depth, then per saved entry, oldest first: IDXA selector, IDXB selector, CFG selector, the three command parameters, the saved IDXA descriptor (16 bytes), the saved IDXB descriptor (16 bytes). |
| `ERRQ` | 1 + count | `u8` count, then the queued error codes, oldest first. |
| `PIO ` | 24 | `u32` `pio0` `FDEBUG`, `u32` `pio1` `FDEBUG`, `u32` `pio0` `FLEVEL`, `u32` `pio1` `FLEVEL`, then the program counters of `pio0` SM0-3 and `pio1` SM0-3 (`u8` each). |
| `TRCE` | 40 + 4 per event | Diagnostic build only: `u32` events, pushes, pops, overflows, underflows, frozen (0 or 1), frozen at, `pio0` `FDEBUG`, `pio1` `FDEBUG`, count; then count raw action words, oldest first. |
| `RAM ` | 262144 | All of MIA RAM, `$00000` first. |
| `END ` | 0 | End of the file. |

## Bus event trace

The trace records what the 6502 did to MIA's registers just before something
went wrong. It needs a store in core 1's action loop for every bus event, so it
is only in the diagnostic build:

```sh
make diag         # builds build-diag/mia.uf2
make flash-diag   # builds it and loads it
```

The diagnostic firmware is the normal firmware plus the trace. Flash the normal
build (`make flash`) again when you are done.

Core 1 keeps the last 64 action events. Each one is a raw action word: the data
byte, the register offset and whether it was a read or a write. On the first
`MIA_CTX` overflow or underflow it freezes a copy of them, with the PIO
`FDEBUG` flags at that moment. It also counts `MIA_CTX` pushes and pops.

`trace` prints the counters and the frozen capture, oldest event first, the
failing `MIA_CTX` write last. When nothing has been captured, it prints the
latest 64 events instead. `trace clear` zeroes the counters, drops the capture
and arms it again.

The action PIO reports every register write but only the reads it watches
(every fourth register, plus the one the current mode watches), so the trace
sees the bus exactly as core 1 does. Reads of unwatched registers are not in
it.

This is how the `*READ` commands' `MIA_CTX` underflow was found: the trace
showed the stray writes `CHRREAD` made into the MIA registers, `MIA_CTX`
among them.
