# MIA Audio Programmer Guide

This guide describes how a Clementina 6502 program uses MIA's 4-voice stereo
PWM PSG. The exact memory layout is defined in [audio.md](audio.md).

## Mental Model

MIA audio behaves like a small programmable sound chip. Your program writes
voice registers in MIA RAM and toggles a gate bit to start or release notes.
MIA handles oscillator stepping, waveforms, envelopes, panning, mixing, and PWM
output.

Audio is not sample playback. The 6502 does not need to feed audio every frame.
For normal music or sound effects, write a few registers when a note changes.

## Constants

```asm
IDXA_PORT       = $FFE0
IDXA_SELECT     = $FFE1
CMD_PARAM1      = $FFE6
CMD_PARAM2      = $FFE7
CMD_PARAM3      = $FFE8
CMD_TRIGGER     = $FFE9

CMD_AUDIO_ENABLE = $60
CMD_AUDIO_STOP   = $61
CMD_AUDIO_RESET  = $62

IIDX_AUDIO_ALL    = $D0
IIDX_AUDIO_CH0    = $D1
IIDX_AUDIO_CH1    = $D2
IIDX_AUDIO_CH2    = $D3
IIDX_AUDIO_CH3    = $D4
IIDX_AUDIO_HEADER = $D5

AUDIO_WAVE_SINE     = $00
AUDIO_WAVE_PULSE    = $01
AUDIO_WAVE_SAW      = $02
AUDIO_WAVE_TRIANGLE = $03
AUDIO_WAVE_NOISE    = $04

AUDIO_GATE        = %00000001
AUDIO_RESET_PHASE = %00000010
```

Each voice index points at an 8-byte record:

| Offset | Register |
| ---: | --- |
| 0 | `FREQ_L` |
| 1 | `FREQ_H` |
| 2 | `PULSE_WIDTH` |
| 3 | `ATTACK_DECAY` |
| 4 | `SUSTAIN_RELEASE` |
| 5 | `WAVEFORM` |
| 6 | `PAN` |
| 7 | `CONTROL` |

## Enabling Audio

Audio starts stopped. Enable it once after setting up any initial voices:

```asm
audio_enable:
    lda #$00
    sta CMD_PARAM1
    sta CMD_PARAM2
    sta CMD_PARAM3
    lda #CMD_AUDIO_ENABLE
    sta CMD_TRIGGER
    rts
```

`AUDIO_ENABLE` synchronizes the live audio engine from the register bytes in MIA
RAM. `AUDIO_STOP` stops the IRQ and preserves the bytes. `AUDIO_RESET` stops
audio and clears the audio block back to defaults.

## Frequency Values

Frequency is stored as unsigned 12.4 fixed-point Hz:

```text
register_value = round(frequency_hz * 16)
frequency_hz   = register_value / 16
```

Examples:

| Note | Frequency | Register value |
| --- | ---: | ---: |
| C4 | 261.63 Hz | `$105A` |
| E4 | 329.63 Hz | `$149A` |
| G4 | 392.00 Hz | `$1880` |
| A4 | 440.00 Hz | `$1B80` |
| C5 | 523.25 Hz | `$20B4` |

The maximum representable frequency is just under 4096 Hz.

## Playing A Note

This example configures voice 0 for a centered pulse wave A4 and gates it on.

```asm
audio_note_a4:
    lda #IIDX_AUDIO_CH0
    sta IDXA_SELECT

    lda #$80            ; FREQ_L: A4 = $1B80
    sta IDXA_PORT
    lda #$1B            ; FREQ_H
    sta IDXA_PORT

    lda #$80            ; PULSE_WIDTH: near 50 percent
    sta IDXA_PORT

    lda #$24            ; ATTACK_DECAY: attack=2, decay=4
    sta IDXA_PORT
    lda #$C5            ; SUSTAIN_RELEASE: sustain=C, release=5
    sta IDXA_PORT

    lda #AUDIO_WAVE_PULSE
    sta IDXA_PORT

    lda #$00            ; PAN: center
    sta IDXA_PORT

    lda #AUDIO_GATE | AUDIO_RESET_PHASE
    sta IDXA_PORT
    rts
```

To release the note, write the control byte with gate cleared:

```asm
audio_note_off_ch0:
    lda #IIDX_AUDIO_CH0
    sta IDXA_SELECT

    lda #$80            ; FREQ_L: keep A4 = $1B80
    sta IDXA_PORT
    lda #$1B            ; FREQ_H
    sta IDXA_PORT
    lda #$80            ; PULSE_WIDTH
    sta IDXA_PORT
    lda #$24            ; ATTACK_DECAY
    sta IDXA_PORT
    lda #$C5            ; SUSTAIN_RELEASE
    sta IDXA_PORT
    lda #AUDIO_WAVE_PULSE
    sta IDXA_PORT
    lda #$00            ; PAN
    sta IDXA_PORT
    lda #$00
    sta IDXA_PORT
    rts
```

For a real music engine, it is usually better to keep a small shadow copy of
each voice in 6502 RAM and rewrite the full 8-byte voice record when a note
starts or stops. Four voices are only 32 bytes total, so whole-record writes are
cheap and predictable. If every event writes all 8 bytes, the fixed voice index
wraps back to the start of the record after each event.

## Envelope Use

`ATTACK_DECAY` and `SUSTAIN_RELEASE` are nibble-packed:

```text
ATTACK_DECAY     = (attack << 4) | decay
SUSTAIN_RELEASE  = (sustain << 4) | release
```

Attack, decay, and release nibbles are rate indexes. Lower values are faster.
Sustain is a level where `$0` is silent and `$F` is full.

Common presets:

| Sound | `ATTACK_DECAY` | `SUSTAIN_RELEASE` | Feel |
| --- | ---: | ---: | --- |
| Pluck | `$03` | `$45` | instant attack, quick decay, medium-low sustain |
| Organ | `$10` | `$F2` | fast attack, full sustain, quick release |
| Pad | `$86` | `$A8` | slow attack, medium sustain, slower release |
| Drum/noise hit | `$01` | `$08` | instant attack, no sustain, short release |

## Waveforms

`PULSE_WIDTH` only affects the pulse waveform. `128` is close to square. Small
values make a narrow positive pulse, and large values make a wide positive
pulse.

Noise is pitched. Higher frequency values update the pseudo-random source more
often and sound brighter.

## Panning

`PAN` is signed:

| Value | Position |
| ---: | --- |
| `$C0` (`-64`) | hard left |
| `$00` | center |
| `$3F` (`63`) | hard right |

Values outside `-64..63` are clamped by MIA.

## Updating Live Audio

When audio is active, writes through `IDXA_PORT` or `IDXB_PORT` to the audio
block are applied by the audio IRQ. Typical latency is one audio sample, about
42 microseconds.

Frequency changes are optimized internally. MIA recalculates the oscillator
phase increment when either frequency byte changes, then the IRQ only adds the
cached increment each sample. This is transparent to programs.

If a program uses MIA's DMA-copy command to bulk-copy audio registers, those
bytes are written to RAM but are not queued as live audio writes. Stop and
enable audio after a bulk copy to resynchronize without clearing the block.
For strict sequencing, issue `AUDIO_STOP`, wait for `IRQ_COMMAND` or otherwise
know the command has completed, then issue `AUDIO_ENABLE`.

## Suggested Engine Shape

A simple music driver can keep four 8-byte voice shadows in ordinary 6502 RAM:

1. Build the target voice record in the shadow.
2. Select `IIDX_AUDIO_CHn`.
3. Write all 8 bytes through `IDXA_PORT`.
4. Set `GATE` for note-on, clear `GATE` for note-off.

This costs only eight indexed writes per voice event and keeps timing simple.
