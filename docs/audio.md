# MIA Audio Subsystem

This document defines MIA's local PWM PSG audio state. The programmer-facing
workflow and examples are in [audio-programmer-guide.md](audio-programmer-guide.md).

## Overview

MIA audio is a small software PSG mixed to stereo PWM output. It is intended for
SID-era programmable sound rather than sample playback or FM synthesis.

The first implementation provides:

| Feature | Value |
| --- | ---: |
| Voices | 4 |
| Output | stereo PWM |
| Sample rate | 24,000 Hz |
| Frequency register | unsigned 12.4 fixed-point Hz |
| Waveforms | sine, pulse, saw, triangle, noise |
| Envelope | attack, decay, sustain, release |
| Panning | signed 8-bit, clamped to `-64..63` |
| Master volume | 4-bit, `0..15` (SID `$D418`-style) |
| Per-voice volume | linear 8-bit, `0..255` |

The audio IRQ runs on core 0. Core 1 only observes indexed RAM writes to the
audio block and queues byte updates for the audio IRQ to consume. This keeps the
6502 bus/action loop cheap and bounded.

## Hardware Pins

The default pin mapping is:

| Signal | Default GPIO | Notes |
| --- | ---: | --- |
| Audio left | 4 | PWM output |
| Audio right | 5 | PWM output |
| Audio IRQ timer | 28 | PWM slice timer only, not driven as audio output |

These defaults can be overridden at build time with `MIA_AUDIO_L_PIN`,
`MIA_AUDIO_R_PIN`, and `MIA_AUDIO_IRQ_PIN`. The IRQ timer pin must map to a PWM
slice that is not used by either audio output pin.

## Memory Map

Audio state lives in MIA RAM at `$12000-$1204F`. It is outside the syncable video
region, so audio writes do not dirty video pages.

| Range | Size | Description |
| ---: | ---: | --- |
| `$12000-$1200F` | 16 | Audio header |
| `$12010-$1201F` | 16 | Voice 0 |
| `$12020-$1202F` | 16 | Voice 1 |
| `$12030-$1203F` | 16 | Voice 2 |
| `$12040-$1204F` | 16 | Voice 3 |

### Header

Offsets in this table are relative to `$12000`.

| Offset | Name | Access | Description |
| ---: | --- | --- | --- |
| `$00` | `AUDIO_VERSION` | read | Audio memory layout version. Current value is `2`. Version `2` widened each voice record from 8 to 16 bytes and added `AUDIO_VOLUME` and per-voice `VOLUME`. |
| `$01` | `AUDIO_VOLUME` | read/write | Master volume, `0..15`. `0` is silent, `15` is full. Only the low nibble is used. Defaults to `15`. |
| `$02` | `AUDIO_STATUS` | read | Audio status flags. |
| `$03` | `AUDIO_CHANNELS` | read | Number of voices. Current value is `4`. |
| `$04-$05` | `AUDIO_SAMPLE_RATE` | read | Little-endian sample rate. Current value is `24000`. |
| `$06` | `AUDIO_FLAGS` | read | Bit 0 set means stereo output is available. |
| `$07-$0F` | reserved | reserved | Write zero. |

`AUDIO_STATUS` flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `AUDIO_STATUS_ACTIVE` | Audio IRQ is running. |
| 1 | `AUDIO_STATUS_QUEUE_OVERFLOW` | One or more live register writes overflowed the audio queue. |

The queue overflow flag is also reported as `ERROR_AUDIO_QUEUE_OVERFLOW`.
Enabling or resetting audio clears the overflow status bit.

### Voice Registers

Each voice is 16 bytes. Offsets in this table are relative to the start of a
voice record. Offsets `$09-$0F` are reserved for future per-voice controls;
write zero.

| Offset | Name | Description |
| ---: | --- | --- |
| `$00` | `FREQ_L` | Low byte of unsigned 12.4 fixed-point frequency in Hz. |
| `$01` | `FREQ_H` | High byte of unsigned 12.4 fixed-point frequency in Hz. |
| `$02` | `PULSE_WIDTH` | Pulse waveform duty threshold, `0..255`. `128` is near 50 percent. |
| `$03` | `ATTACK_DECAY` | High nibble attack rate, low nibble decay rate. |
| `$04` | `SUSTAIN_RELEASE` | High nibble sustain level, low nibble release rate. |
| `$05` | `WAVEFORM` | Waveform selector. |
| `$06` | `PAN` | Signed pan. `-64` left, `0` center, `63` right. |
| `$07` | `CONTROL` | Gate and phase control bits. |
| `$08` | `VOLUME` | Linear per-voice volume, `0..255`. `255` is unity. Defaults to `255`. |
| `$09-$0F` | reserved | Write zero. |

Frequency uses `value = frequency_hz * 16`. For example, A4 at 440 Hz is
`440 * 16 = 7040`, or `$1B80`.

`WAVEFORM` values:

| Value | Name | Description |
| ---: | --- | --- |
| `$00` | sine | 256-entry sine table. |
| `$01` | pulse | Pulse/square wave using `PULSE_WIDTH`. |
| `$02` | saw | Falling sawtooth. |
| `$03` | triangle | Triangle wave. |
| `$04` | noise | Pitched pseudo-random noise. |

`CONTROL` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `GATE` | `1` starts attack, `0` starts release. |
| 1 | `RESET_PHASE` | Reset oscillator phase when this byte is written. |
| 2-7 | reserved | Write zero. |

## Envelope Rates

Attack, decay, and release use 4-bit rate indexes. The timing table follows the
same broad SID-style rate set used by rp6502.

Attack rates:

| Nibble | Approximate time |
| ---: | ---: |
| `$0` | 2 ms |
| `$1` | 8 ms |
| `$2` | 16 ms |
| `$3` | 24 ms |
| `$4` | 38 ms |
| `$5` | 56 ms |
| `$6` | 68 ms |
| `$7` | 80 ms |
| `$8` | 100 ms |
| `$9` | 250 ms |
| `$A` | 500 ms |
| `$B` | 800 ms |
| `$C` | 1000 ms |
| `$D` | 3000 ms |
| `$E` | 5000 ms |
| `$F` | 8000 ms |

Decay and release rates:

| Nibble | Approximate time |
| ---: | ---: |
| `$0` | 6 ms |
| `$1` | 24 ms |
| `$2` | 48 ms |
| `$3` | 72 ms |
| `$4` | 114 ms |
| `$5` | 168 ms |
| `$6` | 204 ms |
| `$7` | 240 ms |
| `$8` | 300 ms |
| `$9` | 750 ms |
| `$A` | 1500 ms |
| `$B` | 2400 ms |
| `$C` | 3000 ms |
| `$D` | 9000 ms |
| `$E` | 15000 ms |
| `$F` | 24000 ms |

Sustain level is a 4-bit linear level where `$0` is silent and `$F` is full.
Attack always rises toward full level, decay falls toward the sustain level, and
release falls toward silence.

## Volume

Two gain stages sit after the envelope:

| Stage | Register | Range | Where it applies |
| --- | --- | ---: | --- |
| Per-voice volume | voice `$08` `VOLUME` | `0..255` linear | Multiplies that voice's post-envelope sample, before panning. |
| Master volume | header `$01` `AUDIO_VOLUME` | `0..15` | Multiplies the summed stereo mix, before the output clamp. |

The full per-voice chain is:

```text
oscillator -> envelope (ADSR) -> VOLUME (0..255) -> PAN -> mix -> AUDIO_VOLUME (0..15) -> clamp
```

The envelope still governs the note contour, and `SUSTAIN` still sets the held
level. `VOLUME` is an independent trim: unlike `SUSTAIN` it also scales the
attack and decay peaks, so it can make a percussive or plucked voice quiet, do
per-voice fades and tremolo, and balance voices without disturbing their
envelopes. `AUDIO_VOLUME` is the SID `$D418`-style master; `0` mutes all output.
Both default to full and both accept live updates while audio is active.

## Indexes

MIA configures fixed indexes for audio during runtime reset:

| Index | Range | Description |
| ---: | ---: | --- |
| `$D0` | `$12000-$1204F` | Whole audio block. |
| `$D1` | `$12010-$1201F` | Voice 0. |
| `$D2` | `$12020-$1202F` | Voice 1. |
| `$D3` | `$12030-$1203F` | Voice 2. |
| `$D4` | `$12040-$1204F` | Voice 3. |
| `$D5` | `$12000-$1200F` | Header. |

All audio indexes step on reads and writes and wrap within their configured
range. Each voice index now spans the full 16-byte record: a program that writes
only the first nine bytes (`FREQ_L` through `VOLUME`) leaves the index parked
mid-record, so re-select the voice index (or write all 16 bytes) before the next
voice event.

## Commands

Audio commands use the normal MIA command registers.

| Command | Id | Parameters | Description |
| --- | ---: | --- | --- |
| `AUDIO_ENABLE` | `$60` | none | Synchronize voice state from audio RAM and start the PWM audio IRQ. |
| `AUDIO_STOP` | `$61` | none | Stop the audio IRQ and return PWM outputs to center. Audio RAM is preserved. |
| `AUDIO_RESET` | `$62` | none | Stop audio, clear the audio RAM block, restore defaults, and reset audio indexes. |

Audio starts stopped after MIA reset. Programs should initialize voice registers
and issue `AUDIO_ENABLE` before playing notes.

## Live Updates

While audio is active, byte writes through `IDXA_PORT` or `IDXB_PORT` into the
audio block are queued and applied by the audio IRQ. This makes changes audible
on the next audio tick, normally within about 42 microseconds at 24 kHz. This
includes the header `AUDIO_VOLUME` byte and each voice's `VOLUME` byte.

Bulk DMA copies inside MIA RAM update the bytes but do not generate live audio
queue entries. If a program bulk-copies a prepared audio register block while
audio is stopped, issue `AUDIO_ENABLE` afterward to resynchronize from RAM. If a
program bulk-copies while audio is active, issue `AUDIO_STOP` then
`AUDIO_ENABLE` to resynchronize without clearing the block.

## Terminal Diagnostics

The USB terminal exposes:

| Command | Description |
| --- | --- |
| `status audio` | Detailed audio status and voice register dump. |
| `audio status` | Same as `status audio`. |
| `audio enable` | Start audio from the terminal. |
| `audio stop` | Stop audio from the terminal. |
| `audio reset` | Clear and reset the audio subsystem. |

