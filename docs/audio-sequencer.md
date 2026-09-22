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

A track has no declared length and no header: it is just bytes at some
`track_base`, decoded live from `mem[]` until an `END`, an unrecognized byte,
or a cursor that runs off the top of MIA RAM stops it. `track_base` is set
per voice by `AUDIO_SEQ_SET_BASE<voice>` (see Commands) and can point
anywhere in MIA RAM — there is no per-track size limit to outgrow, and no
reserved carve-out protecting it from other regions by construction the way
the old fixed layout did. The caller is responsible for picking a base that
doesn't collide with the syncable video region (`$00020-$10D4F`), input/clock
state (`$11000-$1107F`), the audio register block (`$12000-$1204F`), SD/FS
state (`$13000-$13BFF` — control block, sector/path/dir/transfer buffers, see
`sd.md`), or another voice's own track. `$14000-$3FFFF` (~176 KiB) is
otherwise entirely unclaimed and the natural place to put a large or
many-voice song.

Before any `AUDIO_SEQ_SET_BASE<voice>` is issued, `track_base` defaults to a
per-voice address in that free range, so a direct `AUDIO_SEQ_LOAD`/`SEQ_START`
(e.g. the terminal's built-in test track) keeps working unconfigured:

| Range | Description |
| ---: | --- |
| `$14000` | Voice 0 default track base |
| `$15000` | Voice 1 default track base |
| `$16000` | Voice 2 default track base |
| `$17000` | Voice 3 default track base |

(Earlier revisions of this design defaulted to `$13000-$13FFF`, inherited
from the original fixed 1024-byte-per-voice layout. That silently overlapped
live SD/FS state for 3 of the 4 voices — voice 0 aliased the SD/FS control
block, voice 1 the FS dir-entry buffer, voice 2 the FS transfer buffer; only
voice 3 was ever actually clean. The defaults moved here to fix that.)

## Event stream

Opcodes are fixed-format, one tag byte plus a fixed payload. Durations are
**24-bit little-endian sample counts** (up to ~16.7M samples, ~11.6 minutes —
effectively unbounded for a single note) — resolved once at encode time from
the source tempo/length, never recomputed by MIA.

| Opcode | Value | Payload | Effect |
| --- | ---: | --- | --- |
| `END` | `$00` | — | Stop. |
| `NOTE` | `$01` | `freq_l, freq_h, dur_l, dur_m, dur_h` | Write `FREQ_L`/`FREQ_H`, gate on with phase reset, hold for `dur` samples. Advances the note index. |
| `REST` | `$02` | `dur_l, dur_m, dur_h` | Gate off, hold for `dur` samples. Advances the note index. |
| `SET_WAVE` | `$03` | `waveform` | Write `WAVEFORM`. Zero-duration; decoding continues immediately to the next opcode. |
| `SET_ADSR` | `$04` | `attack_decay, sustain_release` | Write both envelope bytes. Zero-duration. |
| `SET_PAN` | `$05` | `pan` | Write `PAN`. Zero-duration. |
| `SET_VOL` | `$06` | `volume` | Write per-voice `VOLUME`. Zero-duration. |
| `SET_PULSE` | `$07` | `pulse_width` | Write `PULSE_WIDTH`. Zero-duration. |
| `JUMP` | `$08` | `offset_l, offset_m, offset_h` | Set cursor to (address right after this record) + `offset`, a **signed 24-bit little-endian** value. Zero-duration; does not advance the note index. |

Every register write an event makes goes through the exact same
`audio_apply_register` path a live 6502 write already uses (mirrored into
`mem[]` first), so gate-edge detection, frequency recompute, and pan recompute
are not reimplemented — the sequencer is just another producer feeding the
same internal voice state live writes already feed.

`NOTE`/`REST` are the only opcodes that advance the **note index** (see
below) and consume time; `SET_*` and `JUMP` are all zero-duration and are
decoded and applied within the same tick, with no delay before the next
opcode, precisely so inserting one never shifts note numbering.

### Looping, and why there is no `LOOP` opcode

"Loop the whole track" (or loop just a repeating body after a non-repeating
intro) is simply a `JUMP` placed wherever playback should return to — there
is no separate loop opcode or header field. `JUMP`'s offset is **self-relative**
(relative to the byte right after its own record, not to `track_base`), the
same way a branch instruction is relative to the next instruction: this makes
a track's internal jumps invariant under relocation, so moving a track to a
different `track_base` via `AUDIO_SEQ_SET_BASE<voice>` never requires
rewriting anything inside it.

One consequence: the note index (`SEQ_NOTE_INDEX`, backing `CUE(v)`) is not
reset when a `JUMP` is taken — it simply keeps counting notes/rests across
however many passes the track has looped, unlike the old header-based `LOOP`
field, which restored a cached index on every wrap. A composer wanting
"position within the repeating body" computes it themselves (e.g. `CUE(v) MOD`
the body's note count) if the track loops; a composer using `CUE` to catch a
one-time transition (the common case — see `basic-sound.md`'s `CUE` example)
is unaffected either way. A track that loops indefinitely for long enough
(65536 notes/rests) will wrap `SEQ_NOTE_INDEX` back through `0`.

### Runaway-jump safety: `MIA_SEQ_DECODE_BUDGET`

Because `JUMP`/`SET_*` are zero-duration and decoding loops straight back for
the next opcode without returning, a track whose jumps form a cycle with no
`NOTE`/`REST`/`END` in between would otherwise spin the shared audio ISR
forever on a single sample. `audio_seq_advance` bounds this to
`MIA_SEQ_DECODE_BUDGET` (32) opcodes per call; exceeding it without reaching
a time-consuming or stopping opcode is treated as a malformed track and stops
it — fail-safe and silent, the same philosophy as an unrecognized opcode
byte. (`audio_seq_catchup_step`'s existing `MIA_SEQ_CATCHUP_BUDGET` loop
already bounds this the same way, incidentally, since it decrements its
budget once per opcode touched regardless of kind.)

## Runtime state (Core 0 only)

Per voice, alongside the existing oscillator/envelope state:

| Field | Purpose |
| --- | --- |
| `running` | Set by `SEQ_START`, cleared by `SEQ_STOP`. Gates whether the voice's event stream advances at all. |
| `taken` | Set by `VOICE_TAKE`, cleared by `VOICE_RELEASE`. While set, the stream doesn't advance, but nothing is silenced — the caller is driving registers directly. |
| `track_base` | Set by `AUDIO_SEQ_SET_BASE<voice>`; defaults to this voice's legacy address (see Memory layout) until then. Where `AUDIO_SEQ_LOAD` resets `cursor` to. |
| `cursor` | Absolute MIA RAM offset of the event currently active. |
| `countdown` | Samples remaining until the current event ends. |
| `event_duration` | Full original duration of the current event (needed to reconcile a `VOICE_RELEASE`, see below). |
| `note_index` | Count of notes/rests decoded so far since this voice was last loaded. `0` means never started; not reset by `JUMP` (see Looping above). |

All of this is decoded and applied inside the audio ISR, at the top of each
sample, before the oscillator/envelope pass — the same point live-write queue
draining already happens. Advancing a per-voice sequencer costs a counter
decrement in the common case, and a handful of extra instructions once per
note; it does not add a new interrupt, a new core, or meaningfully change the
ISR's worst-case cost.

## Commands

The first five take one parameter byte: a voice bitmask (bit *v* = voice
*v*). There's no "0 means all" special case — the caller always spells out
exactly which voices it means (`$0F` for every voice, `1<<v` for one).

| Command | Id | Effect |
| --- | ---: | --- |
| `AUDIO_SEQ_LOAD` | `$63` | For each masked voice: (re)initializes `cursor` to `track_base` and `note_index` to `0`. Does **not** start playback. Issued once by `TRACK v, s$` right after the bytes land in MIA RAM. |
| `AUDIO_SEQ_START` | `$64` | For each masked voice: if it has a loaded track and isn't already running, starts it from wherever `cursor`/`note_index` currently sit — `track_base` right after a `SEQ_LOAD`, or exactly where it was frozen by a prior `SEQ_STOP`. No time is reconciled; this is a plain resume. |
| `AUDIO_SEQ_STOP` | `$65` | For each masked voice: stops advancing its stream and gates it off (silences it). `cursor`/`note_index` are left exactly where they are, so a later `SEQ_START` picks up from there. |
| `AUDIO_VOICE_TAKE` | `$66` | For each masked voice: stops advancing its stream **without** touching its registers — whatever it was doing keeps sounding until the caller's own writes land. Records the current sample clock for `VOICE_RELEASE` to reconcile against. |
| `AUDIO_VOICE_RELEASE` | `$67` | For each masked voice: resumes, reconciling for however much time passed while taken (see Catch-up below). |

`SEQ_START`/`SEQ_STOP` back `BAND n` (mask `$0F`) and `BAND v,n` (mask
`1<<v`) at the BASIC layer identically — a global stop is just a stop of
every voice. `VOICE_TAKE`/`VOICE_RELEASE` back `VTAKE v`/`VGIVE v`.

`AUDIO_SEQ_SET_BASE<voice>` is one command id per voice rather than a shared
bitmask, since a base address is necessarily distinct per voice:

| Command | Id | Effect |
| --- | ---: | --- |
| `AUDIO_SEQ_SET_BASE0` | `$68` | Sets voice 0's `track_base` to the 24-bit little-endian address in params 0-2. |
| `AUDIO_SEQ_SET_BASE1` | `$69` | Same, voice 1. |
| `AUDIO_SEQ_SET_BASE2` | `$6A` | Same, voice 2. |
| `AUDIO_SEQ_SET_BASE3` | `$6B` | Same, voice 3. |

Sets `track_base` only — it does not itself move `cursor`/`note_index`;
follow with `AUDIO_SEQ_LOAD` to (re)start decoding from the new base.

## Loading a track

There's no separate "upload" command: a track is just bytes in MIA RAM,
written the same way any other MIA memory is written from the 6502 — through
an index descriptor. `TRACK v, s$[, addr%]` on the BASIC side encodes the
string to this format and writes it starting at `addr%` if given, or the
default per-voice address otherwise (see Memory layout), issuing
`AUDIO_SEQ_SET_BASE<voice>` first when an explicit address is given. Loading
a track resets that voice's `cursor` to `track_base` and `note_index` to `0`
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
        (following any JUMP crossed along the way)
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
| `$09-$0A` | `SEQ_NOTE_INDEX` | Little-endian count of notes/rests decoded since the track was last loaded, not reset by `JUMP` (see Looping above). `0` = never started. Backs `CUE(v)`. |
| `$0B` | `SEQ_STATUS` | Bit 0: `running`. Bit 1: `taken`. Backs `PLAYING(v)`. |

A program that writes all 16 bytes of a voice record (per the programmer's
guide's "write all 16 bytes" pattern) will write zero into these — harmless,
since the audio ISR rewrites them on the very next sample regardless of what
last landed there; the visible glitch is under 42 µs.

Four new index ids expose these three bytes directly, parked at `$09` by
default so a poll doesn't need to step through the rest of the record first:

| Index | Range |
| ---: | --- |
| `$EC` | Voice 0 offsets `$12019-$1201B` |
| `$ED` | Voice 1 offsets `$12029-$1202B` |
| `$EE` | Voice 2 offsets `$12039-$1203B` |
| `$EF` | Voice 3 offsets `$12049-$1204B` |

## Signaling completion

Bit 14 of `irq_status` (previously unused — see `irq/irq.h`) is
`IRQ_AUDIO_SEQ_DONE`, raised when a voice's track reaches `END` (or an
unrecognized opcode, or a decode/catch-up budget is exhausted). This mirrors
the existing `IRQ_COMMAND`/`IRQ_SD_DONE` pattern: producers
across any core or interrupt context set it via the existing
`mia_irq_set_flag` accumulator, and Core 1's `act_loop` folds it into
`irq_status` on its next pass, same as every other source.

## Errors

No new error codes. A malformed or corrupt track is handled by treating an
out-of-bounds cursor (past the top of MIA RAM), an unrecognized opcode byte,
or an exhausted decode/catch-up budget (a runaway `JUMP` cycle) all as an
implicit `END` — fail-safe, silent, consistent with how a malformed
background `PLAY` string already fails silently today rather than raising a
BASIC error from inside an interrupt context.
