#ifndef _MIA_AUDIO_AUDIO_H_
#define _MIA_AUDIO_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>

/* The PWM interrupt runs the oscillators and the mix at MIA_AUDIO_SAMPLE_RATE.
 * MIA_AUDIO_TICK_RATE is the control clock: the register-write queue, the
 * sequencer and the envelopes step once per tick, on every
 * MIA_AUDIO_SAMPLES_PER_TICK-th sample. NOTE/REST durations and the header's
 * RATE field count ticks, so tracks encoded for 24 kHz play unchanged. */
#define MIA_AUDIO_SAMPLE_RATE 48000u
#define MIA_AUDIO_TICK_RATE 24000u
#define MIA_AUDIO_SAMPLES_PER_TICK (MIA_AUDIO_SAMPLE_RATE / MIA_AUDIO_TICK_RATE)
#define MIA_AUDIO_VOICE_COUNT 4u
#define MIA_AUDIO_VOICE_SIZE 16u

#define MIA_AUDIO_STATE_OFFSET 0x12000u
#define MIA_AUDIO_STATE_SIZE 0x50u
#define MIA_AUDIO_HEADER_OFFSET MIA_AUDIO_STATE_OFFSET
#define MIA_AUDIO_HEADER_SIZE 0x10u
#define MIA_AUDIO_VOICES_OFFSET (MIA_AUDIO_STATE_OFFSET + MIA_AUDIO_HEADER_SIZE)
#define MIA_AUDIO_VOICE_OFFSET(voice) (MIA_AUDIO_VOICES_OFFSET + (voice) * MIA_AUDIO_VOICE_SIZE)

#define MIA_AUDIO_VERSION 2u

#define MIA_AUDIO_HEADER_VERSION 0x00u
#define MIA_AUDIO_HEADER_VOLUME 0x01u
#define MIA_AUDIO_HEADER_STATUS 0x02u
#define MIA_AUDIO_HEADER_CHANNELS 0x03u
#define MIA_AUDIO_HEADER_RATE_L 0x04u
#define MIA_AUDIO_HEADER_RATE_H 0x05u
#define MIA_AUDIO_HEADER_FLAGS 0x06u

#define MIA_AUDIO_STATUS_ACTIVE (1u << 0)
#define MIA_AUDIO_STATUS_QUEUE_OVERFLOW (1u << 1)
#define MIA_AUDIO_FLAG_STEREO (1u << 0)

#define MIA_AUDIO_VOICE_FREQ_L 0x00u
#define MIA_AUDIO_VOICE_FREQ_H 0x01u
#define MIA_AUDIO_VOICE_PULSE_WIDTH 0x02u
#define MIA_AUDIO_VOICE_ATTACK_DECAY 0x03u
#define MIA_AUDIO_VOICE_SUSTAIN_RELEASE 0x04u
#define MIA_AUDIO_VOICE_WAVEFORM 0x05u
#define MIA_AUDIO_VOICE_PAN 0x06u
#define MIA_AUDIO_VOICE_CONTROL 0x07u
#define MIA_AUDIO_VOICE_VOLUME 0x08u

#define MIA_AUDIO_WAVE_SINE 0x00u
#define MIA_AUDIO_WAVE_PULSE 0x01u
#define MIA_AUDIO_WAVE_SAW 0x02u
#define MIA_AUDIO_WAVE_TRIANGLE 0x03u
#define MIA_AUDIO_WAVE_NOISE 0x04u

#define MIA_AUDIO_CONTROL_GATE (1u << 0)
#define MIA_AUDIO_CONTROL_RESET_PHASE (1u << 1)

/* Master volume is a 4-bit level in the header VOLUME byte (0 silent, 15 full),
 * mirroring the SID $D418 master level. Per-voice VOLUME is a linear 8-bit trim
 * (255 = unity) applied after the envelope and before panning. */
#define MIA_AUDIO_MASTER_VOLUME_MAX 0x0Fu
#define MIA_AUDIO_MASTER_VOLUME_DEFAULT 0x0Fu
#define MIA_AUDIO_VOICE_VOLUME_DEFAULT 0xFFu

/* Index IDs $E6-$EF. $E0-$E5 belong to the SD/FS subsystem (sd.h). These must
 * stay clear of $C0-$DF: video pre-configures that whole range as the 32 direct
 * OAM sprite indexes, and audio init runs after video init, so any overlap
 * silently steals sprites from the 6502. */
#define MIA_AUDIO_INDEX_ALL 0xE6u
#define MIA_AUDIO_INDEX_VOICE0 0xE7u
#define MIA_AUDIO_INDEX_VOICE1 0xE8u
#define MIA_AUDIO_INDEX_VOICE2 0xE9u
#define MIA_AUDIO_INDEX_VOICE3 0xEAu
#define MIA_AUDIO_INDEX_HEADER 0xEBu

#define MIA_CMD_AUDIO_ENABLE 0x60u
#define MIA_CMD_AUDIO_STOP 0x61u
#define MIA_CMD_AUDIO_RESET 0x62u

// ---------------------------------------------------------------------------
// Background sequencer (TRACK/BAND/VTAKE/VGIVE at the BASIC layer). See
// docs/audio-sequencer.md for the full contract.
// ---------------------------------------------------------------------------

// A voice's track is just bytes in MIA RAM, decoded live from wherever its
// track_base points until an END or a run off the top of RAM stops it -
// there is no per-track length and no header. track_base is fully
// caller-defined via AUDIO_SEQ_SET_BASE<voice>. Until a program sets it, a
// voice has no track: boot and AUDIO_RESET store MIA_SEQ_NO_BASE, which the
// 24-bit SET_BASE parameter can never produce, and SEQ_START leaves such a
// voice stopped. Nothing in MIA RAM is reserved for tracks, and audio reset
// clears none of it.
#define MIA_SEQ_NO_BASE 0xFFFFFFFFu

// Event opcodes. NOTE/REST durations are 24-bit little-endian tick counts
// (MIA_AUDIO_TICK_RATE per second), resolved once at encode time - MIA never
// interprets tempo or note names.
#define MIA_SEQ_OP_END       0x00u   // stop
#define MIA_SEQ_OP_NOTE      0x01u   // freq_l, freq_h, dur_l, dur_m, dur_h
#define MIA_SEQ_OP_REST      0x02u   // dur_l, dur_m, dur_h
#define MIA_SEQ_OP_SET_WAVE  0x03u   // waveform
#define MIA_SEQ_OP_SET_ADSR  0x04u   // attack_decay, sustain_release
#define MIA_SEQ_OP_SET_PAN   0x05u   // pan
#define MIA_SEQ_OP_SET_VOL   0x06u   // volume
#define MIA_SEQ_OP_SET_PULSE 0x07u   // pulse_width
// JUMP: offset_l, offset_m, offset_h - a signed 24-bit little-endian offset,
// relative to the address of the byte immediately after this 4-byte record
// (self-relative, like a branch, not relative to track_base), so a track
// keeps working unchanged if its base is relocated. Zero-duration - decoding
// continues immediately at the new cursor, and it does not advance
// note_index. "Loop the whole track" is just a JUMP back to the top; there
// is no separate loop opcode or header field.
#define MIA_SEQ_OP_JUMP      0x08u

// Live sequencer status, read through the existing per-voice audio index.
// These reuse voice-record offsets $09-$0B, documented elsewhere as
// reserved/write-zero; the audio ISR rewrites them every tick regardless
// of what last landed there, so a stray zero-write is invisible in practice.
#define MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L 0x09u
#define MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_H 0x0Au
#define MIA_AUDIO_VOICE_SEQ_STATUS 0x0Bu
#define MIA_AUDIO_SEQ_STATUS_RUNNING (1u << 0)
#define MIA_AUDIO_SEQ_STATUS_TAKEN (1u << 1)

// Dedicated indexes parked at voice offset $09, so a status poll doesn't have
// to step through the rest of the record first.
#define MIA_AUDIO_INDEX_SEQ_VOICE0 0xECu
#define MIA_AUDIO_INDEX_SEQ_VOICE1 0xEDu
#define MIA_AUDIO_INDEX_SEQ_VOICE2 0xEEu
#define MIA_AUDIO_INDEX_SEQ_VOICE3 0xEFu

_Static_assert(MIA_AUDIO_INDEX_ALL > 0xDFu,
               "audio indexes must not overlap the $C0-$DF video OAM sprite indexes");

// Commands. Each takes one parameter byte: a voice bitmask (bit v = voice
// v). There is no "0 means all" case - callers always spell out the mask.
#define MIA_CMD_AUDIO_SEQ_LOAD      0x63u
#define MIA_CMD_AUDIO_SEQ_START     0x64u
#define MIA_CMD_AUDIO_SEQ_STOP      0x65u
#define MIA_CMD_AUDIO_VOICE_TAKE    0x66u
#define MIA_CMD_AUDIO_VOICE_RELEASE 0x67u
// AUDIO_SEQ_SET_BASE<voice>: one command id per voice (a base address is
// necessarily distinct per voice, unlike the other commands' shared
// bitmask), each taking a 3-byte little-endian MIA RAM address as its
// parameter. Sets that voice's track_base only; does not itself move
// cursor/note_index - follow with AUDIO_SEQ_LOAD to (re)start decoding from
// the new base.
#define MIA_CMD_AUDIO_SEQ_SET_BASE0 0x68u
#define MIA_CMD_AUDIO_SEQ_SET_BASE1 0x69u
#define MIA_CMD_AUDIO_SEQ_SET_BASE2 0x6Au
#define MIA_CMD_AUDIO_SEQ_SET_BASE3 0x6Bu

// Max events silently skipped per audio tick while reconciling a
// VOICE_RELEASE, so a long TAKE resolves over a few extra ticks instead of
// doing unbounded work inside one sample.
#define MIA_SEQ_CATCHUP_BUDGET 8u

// Max zero-duration opcodes (SET_*/JUMP) decoded in a row within a single
// audio_seq_advance() call before it gives up and stops the voice. Without
// this, a track whose JUMPs form a cycle with no NOTE/REST/END in between
// would spin the shared audio ISR forever on one sample - fail-safe, silent,
// exactly like an unrecognized opcode byte.
#define MIA_SEQ_DECODE_BUDGET 32u

void mia_audio_init(void);
void mia_audio_reset_runtime_state(void);
void mia_audio_reclock(void);

void mia_audio_enable(void);
void mia_audio_stop(void);
void mia_audio_reset(void);

void mia_audio_core1_on_write(uint32_t offset, uint8_t value);

bool mia_audio_is_active(void);
void mia_audio_print_summary(void);
void mia_audio_print_status(void);
// Prints how long the audio interrupt takes (cycle counter, core 0), and
// optionally starts a new measurement window.
void mia_audio_print_meter(bool reset);

void mia_audio_seq_set_base(uint8_t voice, uint32_t base);
void mia_audio_seq_load_track(uint8_t voice);
void mia_audio_seq_start(uint8_t voice_mask);
void mia_audio_seq_stop(uint8_t voice_mask);
void mia_audio_voice_take(uint8_t voice_mask);
void mia_audio_voice_release(uint8_t voice_mask);
void mia_audio_seq_print_status(void);

// Terminal-only helper: writes a small built-in looping arpeggio at base and
// makes it the voice's track (does not load or start it - follow with
// mia_audio_seq_load_track()/mia_audio_seq_start()). For standalone hardware
// bring-up before TRACK/BAND exist on the BASIC side.
void mia_audio_seq_write_test_track(uint8_t voice, uint32_t base);

#endif
