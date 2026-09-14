# MIA Audio Sequencer

This document defines MIA's background note sequencer: a small per-voice
event player layered on top of the PSG described in
[audio.md](audio.md)/[audio-programmer-guide.md](audio-programmer-guide.md).
It lets a 6502 program upload a compact, pre-resolved event stream per voice,
start it, and walk away — MIA times and plays it entirely on its own, in the
same audio ISR that already steps oscillators and envelopes. The BASIC-facing
statements (`TRACK`, `BAND`, `VTAKE`, `VGIVE`, `PLAYING`, `CUE`) are defined in
`clementina-rom`; this document is the MIA-side contract they're built on.

## Why this exists

Without this, background music is a 6502-side interpreter: a program parses a
music-string token by token off a timer IRQ, competing for CPU with everything
else the program is doing. This engine moves that job entirely into MIA:

- The 6502 encodes a voice's part **once**, uploads the bytes, and issues a
  start command. From then on it costs the 6502 nothing until it wants to
  change something.
- MIA never interprets music theory. Tempo, note names, octaves, and note
  lengths are all resolved at encode time into "hold this many samples" —
  the engine only ever does fixed-format opcode decode and register writes,
  the same kind of work `audio_apply_register` already does for live writes.
- The engine lives inside the existing 24 kHz audio ISR on Core 0, the same
  place voice envelopes are already stepped every sample. It does not touch
  Core 1's bus-service loop or the video-over-wifi path in any way, and it
  does not add wifi bandwidth: nothing here leaves MIA.

## Memory layout

Track buffers live outside both the syncable video region (`$00020-$10D4F`)
and the audio register block (`$12000-$1204F`), for the same reason audio
itself does: writes there must never dirty a video page or disturb live voice
registers.

| Range | Size | Description |
| ---: | ---: | --- |
| `$13000-$133FF` | 1024 | Voice 0 track buffer |
| `$13400-$137FF` | 1024 | Voice 1 track buffer |
| `$13800-$13BFF` | 1024 | Voice 2 track buffer |
| `$13C00-$13FFF` | 1024 | Voice 3 track buffer |

Each voice's buffer holds a 4-byte header followed by its event stream:

| Offset | Size | Name | Description |
| ---: | ---: | --- | --- |
| `$00-$01` | 2 | `LOOP` | Little-endian offset, relative to the event stream (byte `$04` of the buffer), to resume at when `END` is reached. `$FFFF` means "no loop — stop." |
| `$02-$03` | 2 | reserved | Write zero. |
| `$04...` | — | events | The event stream, decoded from the top on `SEQ_START`. |

A 1024-byte buffer is generous for hundreds of events; nothing about the
design requires that exact size, it's just comfortably larger than any
reasonable background part while costing a rounding error against MIA's
256 KiB (4 KiB total for all four voices).

## Event stream

Opcodes are fixed-format, one tag byte plus a fixed payload. Durations are
**24-bit little-endian sample counts** (up to ~16.7M samples, ~11.6 minutes —
effectively unbounded for a single note) — resolved once at encode time from
the source tempo/length, never recomputed by MIA.

| Opcode | Value | Payload | Effect |
| --- | ---: | --- | --- |
| `END` | `$00` | — | Stop, or jump to `LOOP` if it isn't `$FFFF`. |
| `NOTE` | `$01` | `freq_l, freq_h, dur_l, dur_m, dur_h` | Write `FREQ_L`/`FREQ_H`, gate on with phase reset, hold for `dur` samples. Advances the note index. |
| `REST` | `$02` | `dur_l, dur_m, dur_h` | Gate off, hold for `dur` samples. Advances the note index. |
| `SET_WAVE` | `$03` | `waveform` | Write `WAVEFORM`. Zero-duration; decoding continues immediately to the next opcode. |
| `SET_ADSR` | `$04` | `attack_decay, sustain_release` | Write both envelope bytes. Zero-duration. |
| `SET_PAN` | `$05` | `pan` | Write `PAN`. Zero-duration. |
| `SET_VOL` | `$06` | `volume` | Write per-voice `VOLUME`. Zero-duration. |
| `SET_PULSE` | `$07` | `pulse_width` | Write `PULSE_WIDTH`. Zero-duration. |

Every register write an event makes goes through the exact same
`audio_apply_register` path a live 6502 write already uses (mirrored into
`mem[]` first), so gate-edge detection, frequency recompute, and pan recompute
are not reimplemented — the sequencer is just another producer feeding the
same internal voice state live writes already feed.

`NOTE`/`REST` are the only opcodes that advance the **note index** (see
below) and consume time; the `SET_*` opcodes are configuration and are
decoded and applied within the same tick, with no delay before the next
opcode, precisely so inserting one never shifts note numbering.

## Runtime state (Core 0 only)

Per voice, alongside the existing oscillator/envelope state:

| Field | Purpose |
| --- | --- |
| `running` | Set by `SEQ_START`, cleared by `SEQ_STOP`. Gates whether the voice's event stream advances at all. |
| `taken` | Set by `VOICE_TAKE`, cleared by `VOICE_RELEASE`. While set, the stream doesn't advance, but nothing is silenced — the caller is driving registers directly. |
| `cursor` | Absolute MIA RAM offset of the event currently active. |
| `countdown` | Samples remaining until the current event ends. |
| `event_duration` | Full original duration of the current event (needed to reconcile a `VOICE_RELEASE`, see below). |
| `note_index` | 1-based index of the current note/rest within the *current pass* of the track. `0` means never started. |
| `loop_offset` / `loop_note_index` | Cached from the track header at `SEQ_START`/on load: where to jump back to, and what note index to restore, when `END` is reached. |

All of this is decoded and applied inside the audio ISR, at the top of each
sample, before the oscillator/envelope pass — the same point live-write queue
draining already happens. Advancing a per-voice sequencer costs a counter
decrement in the common case, and a handful of extra instructions once per
note; it does not add a new interrupt, a new core, or meaningfully change the
ISR's worst-case cost.

## Commands

All four take one parameter byte: a voice bitmask (bit *v* = voice *v*).
There's no "0 means all" special case — the caller always spells out exactly
which voices it means (`$0F` for every voice, `1<<v` for one).

| Command | Id | Effect |
| --- | ---: | --- |
| `AUDIO_SEQ_LOAD` | `$63` | For each masked voice: (re)initializes `cursor` to the top of its event stream and `note_index` to `0`, and caches `LOOP` from the buffer header. Does **not** start playback. Issued once by `TRACK v, s$` right after the bytes land in MIA RAM. |
| `AUDIO_SEQ_START` | `$64` | For each masked voice: if it has a loaded track and isn't already running, starts it from wherever `cursor`/`note_index` currently sit — `$00` right after a `SEQ_LOAD`, or exactly where it was frozen by a prior `SEQ_STOP`. No time is reconciled; this is a plain resume. |
| `AUDIO_SEQ_STOP` | `$65` | For each masked voice: stops advancing its stream and gates it off (silences it). `cursor`/`note_index` are left exactly where they are, so a later `SEQ_START` picks up from there. |
| `AUDIO_VOICE_TAKE` | `$66` | For each masked voice: stops advancing its stream **without** touching its registers — whatever it was doing keeps sounding until the caller's own writes land. Records the current sample clock for `VOICE_RELEASE` to reconcile against. |
| `AUDIO_VOICE_RELEASE` | `$67` | For each masked voice: resumes, reconciling for however much time passed while taken (see Catch-up below). |

`SEQ_START`/`SEQ_STOP` back `BAND n` (mask `$0F`) and `BAND v,n` (mask
`1<<v`) at the BASIC layer identically — a global stop is just a stop of
every voice. `VOICE_TAKE`/`VOICE_RELEASE` back `VTAKE v`/`VGIVE v`.

## Loading a track

There's no separate "upload" command: a track is just bytes in MIA RAM,
written the same way any other MIA memory is written from the 6502 — through
an index descriptor. `TRACK v, s$` on the BASIC side encodes the string to
this format and writes it through the existing indexed-RAM path (a bulk DMA
copy from a staging area, the same mechanism `BLOAD`/`BSAVE` already use, is
the natural choice for anything past a few bytes). Loading a track resets
that voice's `cursor` to the top of its event stream and `note_index` to `0`
— it does not itself start playback; that's what `SEQ_START` is for. This is
what lets `TRACK` + wait-for-a-`CUE`-value + `SEQ_START` compose into "swap
this voice's part in exactly on beat."

## Catch-up (`VOICE_RELEASE` reconciliation)

`VOICE_TAKE`/`VOICE_RELEASE` chose "catch up to real time" over "freeze and
resume exactly where it paused": a voice given back after an SFX should land
wherever it would be if it had kept running silently the whole time, not
wherever it happened to be the instant it was borrowed.

Reconciling this correctly needs one more number than the resume in
`SEQ_START` does: not just "how long was it taken," but "how far into its
current event was it already, when taken" — otherwise the catch-up
undercounts real elapsed time by however much of that event had already
played, and can land one event later than it should.

On `VOICE_TAKE`, MIA snapshots the moment (a free-running sample counter,
internal to the sequencer, never exposed to BASIC — `CUE` reports a
per-voice *note index*, not this). On `VOICE_RELEASE`:

```text
already_spent   = event_duration - countdown      # progress into the current event at take-time
total_elapsed   = already_spent + (now - taken_at)

if total_elapsed < event_duration:
    # Real time hasn't reached this event's natural end yet.
    countdown = event_duration - total_elapsed     # resume the same event, mid-flight, no retrigger
else:
    # This event is over. Walk forward, skipping whatever's fully in the past.
    budget = total_elapsed - event_duration
    advance cursor to the next event
    while budget covers the next event's full duration:
        subtract it from budget, advance the note index and cursor
        (wrapping via LOOP if an END is crossed)
    decode and apply the first event budget doesn't fully cover, in full, live
```

The walk-forward loop never truncates a note to make up time — it skips
whole events that are entirely in the past and starts the first one that
isn't, fresh, rather than resuming it partway through. Cutting a note short
reads as a glitch; starting the next one essentially on time does not.

That loop is bounded to `MIA_SEQ_CATCHUP_BUDGET` (8) events per audio tick.
A voice taken for a very long SFX with a dense part underneath it resolves
over a handful of extra ticks (each ~42 µs) instead of doing unbounded work
inside one sample — the audio ISR's worst-case cost per tick stays bounded
regardless of how long a voice was taken for.

## Reading status: `PLAYING(v)` and `CUE(v)`

Both are plain memory reads through the **existing** per-voice audio index —
no command round-trip, so they're cheap enough to poll in a tight loop.
Offsets `$09-$0B` of each voice's 16-byte record (documented as reserved,
write-zero, in `audio.md`) now carry live sequencer status, continuously
rewritten by the audio ISR every sample:

| Voice offset | Name | Description |
| ---: | --- | --- |
| `$09-$0A` | `SEQ_NOTE_INDEX` | Little-endian, 1-based index of the current note/rest within the current pass of the loop. `0` = never started. Backs `CUE(v)`. |
| `$0B` | `SEQ_STATUS` | Bit 0: `running`. Bit 1: `taken`. Backs `PLAYING(v)`. |

A program that writes all 16 bytes of a voice record (per the programmer's
guide's "write all 16 bytes" pattern) will write zero into these — harmless,
since the audio ISR rewrites them on the very next sample regardless of what
last landed there; the visible glitch is under 42 µs.

Four new index ids expose these three bytes directly, parked at `$09` by
default so a poll doesn't need to step through the rest of the record first:

| Index | Range |
| ---: | --- |
| `$D6` | Voice 0 offsets `$12019-$1201B` |
| `$D7` | Voice 1 offsets `$12029-$1202B` |
| `$D8` | Voice 2 offsets `$12039-$1203B` |
| `$D9` | Voice 3 offsets `$12049-$1204B` |

## Signaling completion

Bit 14 of `irq_status` (previously unused — see `irq/irq.h`) is
`IRQ_AUDIO_SEQ_DONE`, raised when a voice's track reaches `END` with no loop
set. This mirrors the existing `IRQ_COMMAND`/`IRQ_SD_DONE` pattern: producers
across any core or interrupt context set it via the existing
`mia_irq_set_flag` accumulator, and Core 1's `act_loop` folds it into
`irq_status` on its next pass, same as every other source.

## Errors

No new error codes. A malformed or corrupt track is handled by treating an
out-of-bounds cursor as an implicit `END` (stop, no loop) rather than reading
past the voice's 1024-byte buffer — fail-safe, silent, consistent with how a
malformed background `PLAY` string already fails silently today rather than
raising a BASIC error from inside an interrupt context.
