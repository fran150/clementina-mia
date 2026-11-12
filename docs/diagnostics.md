# Diagnostics

These console commands look inside MIA while a program runs. They answer
questions such as "which index was selected when this went wrong" or "what did
the 6502 write to MIA just before the error" without a special build or a logic
analyzer.

## They do not touch the bus

Core 1 runs the action loop that answers the 6502's register accesses. The
console runs on core 0, and every command here runs there. It only reads what
core 1 and the PIOs already keep, so core 1 has nothing extra to do. (The
timing behind `status sys` adds a few register reads to core 0's main loop,
not to core 1's.) The
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
| `status sys` | [Resource use](#resource-use): flash, SRAM, heap, stack peaks, clock, temperature, Wi-Fi signal, reset reason, where core 0's time goes, and how close core 1 comes to dropping bus events. |
| `status sys clear` | Reset the peaks: slowest times and the action FIFO peak. |
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

## Resource use

`status sys` prints what MIA has left and how busy its two cores are:

```
System:
  uptime:   0d 01:12:09
  reset:    power-on
  clock:    256 MHz
  temp:     31.4 C (chip sensor, about +/-2 C)
  wifi:     -58 dBm
  flash:    503 KB of 4096 KB (12%)  firmware
  RAM:      346 KB of 512 KB (67%)  static, 256 KB of it MIA RAM
  heap:     1204 bytes in use, 164 KB free
  stacks:   core 0 1312 of 2048 bytes, core 1 96 of 2048 bytes (deepest since boot)
Core 0 main loop (share of the last 1000 ms, slowest since clear):
  mia        6.1%    12 us
  sd         0.4%   980 us
  wifi      31.0%   214 us
  video     42.7%    85 us
  console    1.2%    23 ms
  other     18.6%     3 us
  passes:  41000 per second, slowest   23 ms
Core 1 bus loop:
  FIFO:     peak 1 of 4 events waiting since clear; no events lost
  busy:     only in the diagnostic build (make diag)
```

(The numbers are an illustration, not a measurement.)

- **flash** is the firmware image; nothing else is stored in flash. About
  224 KB of it is the Wi-Fi chip's own firmware.
- **RAM** is the RP2350's main SRAM taken by static data. The 256 KB MIA RAM
  array is part of it. The rest is the heap, which only the C library uses.
- **stacks** are the deepest each core's 2 KB stack has gone since boot. At
  boot both stacks are filled with a pattern; the command finds the deepest
  word that changed. `FULL` means the stack reached its end and may have
  overwritten what lies below it.
- **wifi** shows only when MIA is joined to a network as a station.
- **reset** names the RP2350's reset flags: `power-on`, `RUN pin`,
  `watchdog` (also how `quit` reboots to BOOTSEL), `software`, `brown-out`
  and a few rarer ones.

### Core 0

Core 0's main loop never sleeps: it goes round and asks each part whether it
has work. A "% busy" would always read 100%, so `status sys` shows where the
time goes instead. Each part of the loop is timed with the core's cycle
counter, and the table gives each one's share of the last complete second and
its slowest single run since boot or `status sys clear`:

| Part | What runs there |
| ---- | --------------- |
| `mia` | Reset requests, the switch to normal mode, the error queue, speed changes, input. |
| `sd` | The 6502's SD and file requests. While the card is slow, Wi-Fi, video and input keep being served from inside this part, so their time counts here. |
| `wifi` | The Wi-Fi driver and the network stack (`cyw43_arch_poll`). |
| `video` | Frames and packets to the video client. |
| `console` | Console input and commands, `status sys` itself included. A command that waits, such as `wifi connect`, shows up as a slow console run. |
| `other` | The LED blink and the loop itself. |

The slowest pass is how long the 6502 may wait at worst for anything core 0
does: a command result, an SD request, an input refresh. Timing the parts
costs core 0 two register reads per part; core 1 is not involved.

### Core 1

Core 1 also never sleeps: it waits for bus events from the action state
machine. The normal firmware does not count anything there. Instead core 0
reads the action FIFO level once per pass and keeps the highest: how many bus
events were ever waiting for core 1 at once. The FIFO holds 4; when it is full
the next event stalls the state machine and is lost, which `RXSTALL` (and the
`no events lost` / `events LOST` note) reports.

In the diagnostic build core 1 also times, with its own cycle counter, every
loop pass that handled a bus event, and counts the events. `status sys` then
shows `busy`, the share of the last second core 1 spent on such passes, and
the events per second. That is the only resource figure that adds work to the
action loop, so it stays out of the normal firmware.

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
