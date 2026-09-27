#include "audio/audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio_mapping.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "etc/err.h"
#include "etc/status.h"
#include "irq/irq.h"
#include "mem/indexes.h"
#include "mem/mem.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AUDIO_PWM_BITS 10u
#define AUDIO_PWM_CENTER (1u << (AUDIO_PWM_BITS - 1u))
#define AUDIO_QUEUE_SIZE 64u
#define AUDIO_ENV_MAX (256u << 16)

#define AUDIO_L_SLICE (pwm_gpio_to_slice_num(MIA_AUDIO_L_PIN))
#define AUDIO_L_CHAN (pwm_gpio_to_channel(MIA_AUDIO_L_PIN))
#define AUDIO_R_SLICE (pwm_gpio_to_slice_num(MIA_AUDIO_R_PIN))
#define AUDIO_R_CHAN (pwm_gpio_to_channel(MIA_AUDIO_R_PIN))
#define AUDIO_IRQ_SLICE (pwm_gpio_to_slice_num(MIA_AUDIO_IRQ_PIN))

#define AUDIO_RATE(ms) ((uint32_t)(((uint64_t)AUDIO_ENV_MAX * 1000u) / ((uint64_t)MIA_AUDIO_SAMPLE_RATE * (ms))))

typedef enum {
    audio_release = 0,
    audio_attack,
    audio_decay,
    audio_sustain,
} audio_adsr_state_t;

typedef struct {
    uint8_t loc;
    uint8_t value;
} audio_queue_entry_t;

typedef struct {
    uint16_t freq_q4;
    uint32_t phase_inc;
    uint8_t pulse_width;
    uint8_t volume;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t waveform;
    int8_t pan;
    uint8_t control;
    uint8_t pan_l;
    uint8_t pan_r;

    int8_t sample;
    audio_adsr_state_t adsr;
    uint32_t vol;
    uint32_t phase;
    uint32_t noise1;
    uint32_t noise2;
} audio_voice_t;

// Background sequencer state, one per voice. See docs/audio-sequencer.md.
// Owned entirely by Core 0 (decoded/applied from inside the audio ISR); the
// command handlers below that mutate it (SEQ_START/STOP, VOICE_TAKE/RELEASE)
// also run on Core 0, via the same FIFO-IRQ command dispatch every other
// audio command already uses, so there is no cross-core sharing to guard.
typedef struct {
    bool running;              // SEQ_START issued, not yet SEQ_STOP'd
    bool taken;                // VOICE_TAKE issued, not yet VOICE_RELEASE'd
    bool catching_up;          // VOICE_RELEASE is silently skipping past elapsed events
    uint32_t track_base;       // this voice's track base, set by AUDIO_SEQ_SET_BASE<voice>
    uint32_t cursor;           // absolute MIA RAM offset of the current event
    uint32_t countdown;        // samples remaining until the current event ends
    uint32_t event_duration;   // full duration of the current event (0 for none yet)
    uint32_t catchup_remaining;// samples still to silently skip during catch-up
    uint32_t taken_at;         // audio_seq_clock snapshot when VOICE_TAKE was issued
    uint16_t note_index;       // 1-based index of the current note/rest; 0 = not started
} audio_seq_t;

static int8_t audio_sine_table[256];
static audio_voice_t audio_voices[MIA_AUDIO_VOICE_COUNT];
static audio_seq_t audio_seq[MIA_AUDIO_VOICE_COUNT];
static uint32_t audio_seq_clock;   // free-running sample counter, internal only - never exposed to BASIC
static uint8_t audio_master_volume = MIA_AUDIO_MASTER_VOLUME_DEFAULT;

static volatile bool audio_active;
static volatile bool audio_queue_overflow;
static volatile uint8_t audio_queue_head;
static volatile uint8_t audio_queue_tail;
static volatile audio_queue_entry_t audio_queue[AUDIO_QUEUE_SIZE];

static const uint32_t audio_level_table[16] = {
    0u << 16,
    17u << 16,
    34u << 16,
    51u << 16,
    68u << 16,
    85u << 16,
    102u << 16,
    119u << 16,
    137u << 16,
    154u << 16,
    171u << 16,
    188u << 16,
    205u << 16,
    222u << 16,
    239u << 16,
    256u << 16,
};

static const uint32_t audio_attack_table[16] = {
    AUDIO_RATE(2),
    AUDIO_RATE(8),
    AUDIO_RATE(16),
    AUDIO_RATE(24),
    AUDIO_RATE(38),
    AUDIO_RATE(56),
    AUDIO_RATE(68),
    AUDIO_RATE(80),
    AUDIO_RATE(100),
    AUDIO_RATE(250),
    AUDIO_RATE(500),
    AUDIO_RATE(800),
    AUDIO_RATE(1000),
    AUDIO_RATE(3000),
    AUDIO_RATE(5000),
    AUDIO_RATE(8000),
};

static const uint32_t audio_decay_release_table[16] = {
    AUDIO_RATE(6),
    AUDIO_RATE(24),
    AUDIO_RATE(48),
    AUDIO_RATE(72),
    AUDIO_RATE(114),
    AUDIO_RATE(168),
    AUDIO_RATE(204),
    AUDIO_RATE(240),
    AUDIO_RATE(300),
    AUDIO_RATE(750),
    AUDIO_RATE(1500),
    AUDIO_RATE(2400),
    AUDIO_RATE(3000),
    AUDIO_RATE(9000),
    AUDIO_RATE(15000),
    AUDIO_RATE(24000),
};

static void audio_configure_indexes(void);
static void audio_configure_index(uint8_t index_id, uint32_t start, uint32_t length);
static void audio_sync_from_memory(void);
static void audio_apply_register(uint8_t loc, uint8_t value);
static void audio_set_irq_rate(void);

static uint16_t audio_read_u16(uint32_t offset) {
    return (uint16_t)mem[offset] | ((uint16_t)mem[offset + 1u] << 8);
}

static void audio_write_u16(uint32_t offset, uint16_t value) {
    mem[offset] = value & 0xFFu;
    mem[offset + 1u] = value >> 8;
}

static uint32_t audio_phase_inc(uint16_t freq_q4) {
    if (freq_q4 == 0) {
        return 0;
    }

    return (uint32_t)(((uint64_t)freq_q4 << 32) /
                      ((uint64_t)MIA_AUDIO_SAMPLE_RATE * 16u));
}

// Master volume maps 0..15 to a 0..255 gain (unity at 15), mirroring the SID
// $D418 master level. Applied to the summed stereo mix before the output clamp.
static inline uint16_t audio_master_gain(void) {
    return (uint16_t)(audio_master_volume & MIA_AUDIO_MASTER_VOLUME_MAX) * 17u;
}

static uint32_t audio_voice_base(uint8_t voice) {
    return MIA_AUDIO_VOICE_OFFSET(voice);
}

static void audio_update_pan(audio_voice_t *voice, int8_t pan) {
    if (pan < -64) {
        pan = -64;
    }
    if (pan > 63) {
        pan = 63;
    }

    voice->pan = pan;
    voice->pan_l = (uint8_t)(64 - pan);
    voice->pan_r = (uint8_t)(64 + pan);
}

static void audio_reset_voice_state(uint8_t voice) {
    audio_voice_t *state = &audio_voices[voice];

    memset(state, 0, sizeof(*state));
    state->pulse_width = 128;
    state->volume = MIA_AUDIO_VOICE_VOLUME_DEFAULT;
    state->adsr = audio_release;
    state->noise1 = 0x67452301u + (uint32_t)voice * 0x11111111u;
    state->noise2 = 0xEFCDAB89u - (uint32_t)voice * 0x01010101u;
    audio_update_pan(state, 0);
}

static void audio_sync_voice_registers(uint8_t voice) {
    audio_voice_t *state = &audio_voices[voice];
    uint32_t base = audio_voice_base(voice);
    uint16_t freq_q4 = audio_read_u16(base + MIA_AUDIO_VOICE_FREQ_L);
    uint8_t attack_decay = mem[base + MIA_AUDIO_VOICE_ATTACK_DECAY];
    uint8_t sustain_release = mem[base + MIA_AUDIO_VOICE_SUSTAIN_RELEASE];
    uint8_t control = mem[base + MIA_AUDIO_VOICE_CONTROL];
    bool old_gate = (state->control & MIA_AUDIO_CONTROL_GATE) != 0;
    bool new_gate = (control & MIA_AUDIO_CONTROL_GATE) != 0;

    state->freq_q4 = freq_q4;
    state->phase_inc = audio_phase_inc(freq_q4);
    state->pulse_width = mem[base + MIA_AUDIO_VOICE_PULSE_WIDTH];
    state->volume = mem[base + MIA_AUDIO_VOICE_VOLUME];
    state->attack = attack_decay >> 4;
    state->decay = attack_decay & 0x0Fu;
    state->sustain = sustain_release >> 4;
    state->release = sustain_release & 0x0Fu;
    state->waveform = mem[base + MIA_AUDIO_VOICE_WAVEFORM] & 0x0Fu;
    audio_update_pan(state, (int8_t)mem[base + MIA_AUDIO_VOICE_PAN]);

    if (control & MIA_AUDIO_CONTROL_RESET_PHASE) {
        state->phase = 0;
    }

    if (!old_gate && new_gate) {
        state->adsr = audio_attack;
        state->vol = 0;
    } else if (old_gate && !new_gate) {
        state->adsr = audio_release;
    }

    state->control = control;
}

static void audio_sync_from_memory(void) {
    audio_master_volume = mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_VOLUME];
    for (uint8_t voice = 0; voice < MIA_AUDIO_VOICE_COUNT; voice++) {
        audio_sync_voice_registers(voice);
    }
}

static void audio_update_frequency(uint8_t voice) {
    uint32_t base = audio_voice_base(voice);
    audio_voices[voice].freq_q4 = audio_read_u16(base + MIA_AUDIO_VOICE_FREQ_L);
    audio_voices[voice].phase_inc = audio_phase_inc(audio_voices[voice].freq_q4);
}

static void audio_apply_register(uint8_t loc, uint8_t value) {
    if (loc == MIA_AUDIO_HEADER_VOLUME) {
        audio_master_volume = value;
        return;
    }

    if (loc < MIA_AUDIO_HEADER_SIZE ||
        loc >= MIA_AUDIO_HEADER_SIZE + MIA_AUDIO_VOICE_COUNT * MIA_AUDIO_VOICE_SIZE) {
        return;
    }

    uint8_t rel = loc - MIA_AUDIO_HEADER_SIZE;
    uint8_t voice = rel / MIA_AUDIO_VOICE_SIZE;
    uint8_t field = rel % MIA_AUDIO_VOICE_SIZE;

    if (voice >= MIA_AUDIO_VOICE_COUNT) {
        return;
    }

    audio_voice_t *state = &audio_voices[voice];

    switch (field) {
        case MIA_AUDIO_VOICE_FREQ_L:
        case MIA_AUDIO_VOICE_FREQ_H:
            audio_update_frequency(voice);
            break;
        case MIA_AUDIO_VOICE_PULSE_WIDTH:
            state->pulse_width = value;
            break;
        case MIA_AUDIO_VOICE_VOLUME:
            state->volume = value;
            break;
        case MIA_AUDIO_VOICE_ATTACK_DECAY:
            state->attack = value >> 4;
            state->decay = value & 0x0Fu;
            break;
        case MIA_AUDIO_VOICE_SUSTAIN_RELEASE:
            state->sustain = value >> 4;
            state->release = value & 0x0Fu;
            break;
        case MIA_AUDIO_VOICE_WAVEFORM:
            state->waveform = value & 0x0Fu;
            break;
        case MIA_AUDIO_VOICE_PAN:
            audio_update_pan(state, (int8_t)value);
            break;
        case MIA_AUDIO_VOICE_CONTROL: {
            bool old_gate = (state->control & MIA_AUDIO_CONTROL_GATE) != 0;
            bool new_gate = (value & MIA_AUDIO_CONTROL_GATE) != 0;

            if (value & MIA_AUDIO_CONTROL_RESET_PHASE) {
                state->phase = 0;
            }
            if (!old_gate && new_gate) {
                state->adsr = audio_attack;
                state->vol = 0;
            } else if (old_gate && !new_gate) {
                state->adsr = audio_release;
            }
            state->control = value;
            break;
        }
        default:
            break;
    }
}

static void audio_drain_queue(uint8_t max_work) {
    if (audio_queue_overflow) {
        audio_sync_from_memory();
        audio_queue_tail = audio_queue_head;
        audio_queue_overflow = false;
        mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_STATUS] |= MIA_AUDIO_STATUS_QUEUE_OVERFLOW;
        return;
    }

    while (max_work-- && audio_queue_tail != audio_queue_head) {
        uint8_t tail = audio_queue_tail;
        audio_queue_entry_t entry = audio_queue[tail];
        audio_queue_tail = (tail + 1u) & (AUDIO_QUEUE_SIZE - 1u);
        audio_apply_register(entry.loc, entry.value);
    }
}

static inline void audio_update_envelope(audio_voice_t *voice) {
    uint32_t sustain_target = audio_level_table[voice->sustain];

    switch (voice->adsr) {
        case audio_attack:
            voice->vol += audio_attack_table[voice->attack];
            if (voice->vol >= AUDIO_ENV_MAX) {
                voice->vol = AUDIO_ENV_MAX;
                voice->adsr = audio_decay;
            }
            break;
        case audio_decay:
            if (voice->vol <= sustain_target) {
                voice->vol = sustain_target;
                voice->adsr = audio_sustain;
            } else {
                uint32_t rate = audio_decay_release_table[voice->decay];
                if (voice->vol <= sustain_target + rate) {
                    voice->vol = sustain_target;
                    voice->adsr = audio_sustain;
                } else {
                    voice->vol -= rate;
                }
            }
            break;
        case audio_sustain:
            voice->vol = sustain_target;
            break;
        case audio_release:
        default: {
            uint32_t rate = audio_decay_release_table[voice->release];
            if (voice->vol <= rate) {
                voice->vol = 0;
            } else {
                voice->vol -= rate;
            }
            break;
        }
    }
}

static inline int8_t audio_next_sample(audio_voice_t *voice) {
    uint32_t old_phase = voice->phase;
    voice->phase += voice->phase_inc;
    uint8_t phase = voice->phase >> 24;

    switch (voice->waveform) {
        case MIA_AUDIO_WAVE_SINE:
            return audio_sine_table[phase];
        case MIA_AUDIO_WAVE_PULSE:
            return phase < voice->pulse_width ? 127 : -127;
        case MIA_AUDIO_WAVE_SAW:
            return (int8_t)(127 - phase);
        case MIA_AUDIO_WAVE_TRIANGLE:
            if (phase < 128u) {
                return (int8_t)((int16_t)phase * 2 - 128);
            }
            return (int8_t)(127 - ((int16_t)(phase - 128u) * 2));
        case MIA_AUDIO_WAVE_NOISE:
            if (voice->phase < old_phase) {
                voice->noise1 ^= voice->noise2;
                voice->noise2 += voice->noise1;
                voice->sample = (int8_t)(voice->noise2 & 0xFFu);
            }
            return voice->sample;
        default:
            return 0;
    }
}

/**************************************************************************************************
 * Background sequencer (TRACK/BAND/VTAKE/VGIVE at the BASIC layer)
 *
 * See docs/audio-sequencer.md for the full contract. Decoding runs inside
 * this same audio ISR, once per voice per sample, right before the
 * oscillator/envelope pass - the same point live register writes are already
 * drained. Every register change an event makes goes through
 * audio_seq_write_reg(), which mirrors mem[] and calls audio_apply_register()
 * so gate-edge detection, frequency recompute, and pan recompute are never
 * reimplemented here.
 **************************************************************************************************/

static void audio_seq_write_reg(uint8_t voice, uint8_t field, uint8_t value) {
    uint32_t base = audio_voice_base(voice);
    mem[base + field] = value;
    audio_apply_register((uint8_t)(base + field - MIA_AUDIO_STATE_OFFSET), value);
}

static void audio_seq_gate(uint8_t voice, bool on) {
    audio_seq_write_reg(voice, MIA_AUDIO_VOICE_CONTROL,
                         on ? (MIA_AUDIO_CONTROL_GATE | MIA_AUDIO_CONTROL_RESET_PHASE) : 0);
}

static void audio_seq_apply_note(uint8_t voice, uint32_t cursor) {
    audio_seq_write_reg(voice, MIA_AUDIO_VOICE_FREQ_L, mem[cursor + 1]);
    audio_seq_write_reg(voice, MIA_AUDIO_VOICE_FREQ_H, mem[cursor + 2]);
    audio_seq_gate(voice, true);
}

static void audio_seq_apply_config_op(uint8_t voice, uint32_t cursor, uint8_t op) {
    switch (op) {
        case MIA_SEQ_OP_SET_WAVE:
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_WAVEFORM, mem[cursor + 1]);
            break;
        case MIA_SEQ_OP_SET_ADSR:
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_ATTACK_DECAY, mem[cursor + 1]);
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_SUSTAIN_RELEASE, mem[cursor + 2]);
            break;
        case MIA_SEQ_OP_SET_PAN:
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_PAN, mem[cursor + 1]);
            break;
        case MIA_SEQ_OP_SET_VOL:
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_VOLUME, mem[cursor + 1]);
            break;
        case MIA_SEQ_OP_SET_PULSE:
            audio_seq_write_reg(voice, MIA_AUDIO_VOICE_PULSE_WIDTH, mem[cursor + 1]);
            break;
        default:
            break;
    }
}

// Returns the number of bytes the opcode at [cursor] occupies, opcode byte
// included, or 0 for END/unrecognized so callers can detect the stop
// condition without a second switch.
static uint8_t audio_seq_record_size(uint8_t op) {
    switch (op) {
        case MIA_SEQ_OP_NOTE:      return 6u;  // op + freq_l + freq_h + dur(3)
        case MIA_SEQ_OP_REST:      return 4u;  // op + dur(3)
        case MIA_SEQ_OP_SET_WAVE:  return 2u;
        case MIA_SEQ_OP_SET_ADSR:  return 3u;
        case MIA_SEQ_OP_SET_PAN:   return 2u;
        case MIA_SEQ_OP_SET_VOL:   return 2u;
        case MIA_SEQ_OP_SET_PULSE: return 2u;
        case MIA_SEQ_OP_JUMP:      return 4u; // op + offset(3)
        default:                   return 0u;
    }
}

static uint32_t audio_seq_read_duration24(uint32_t offset) {
    return (uint32_t)mem[offset] | ((uint32_t)mem[offset + 1] << 8) | ((uint32_t)mem[offset + 2] << 16);
}

// audio_seq_jump_target: resolves a JUMP's 3-byte signed offset (stored at
// cursor+1) into an absolute cursor, relative to the address right after
// this 4-byte record. A malformed offset that lands outside MIA RAM is
// caught by the ordinary cursor >= MIA_RAM_SIZE check on the next iteration,
// same as any other corrupt cursor.
static uint32_t audio_seq_jump_target(uint32_t cursor) {
    uint32_t raw = audio_seq_read_duration24(cursor + 1);
    int32_t offset = (int32_t)(raw << 8) >> 8; // sign-extend bit 23 into 32 bits
    return (uint32_t)((int32_t)(cursor + 4u) + offset);
}

static void audio_seq_write_status(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];
    uint32_t base = audio_voice_base(voice);

    mem[base + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L] = (uint8_t)(seq->note_index & 0xFFu);
    mem[base + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_H] = (uint8_t)(seq->note_index >> 8);
    mem[base + MIA_AUDIO_VOICE_SEQ_STATUS] =
        (uint8_t)((seq->running ? MIA_AUDIO_SEQ_STATUS_RUNNING : 0u) |
                  (seq->taken ? MIA_AUDIO_SEQ_STATUS_TAKEN : 0u));
}

static void audio_seq_finish_track(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];
    seq->running = false;
    seq->catching_up = false;
    audio_seq_gate(voice, false);
    mia_irq_set_flag(IRQ_AUDIO_SEQ_DONE);
}

// Decodes and applies config/JUMP opcodes at the current cursor, live, until
// a NOTE/REST becomes the new current (audible) event, or END/an unrecognized
// byte/running off the top of RAM stops the track. Used for normal playback,
// once the previous event's countdown hits zero.
//
// Bounded by MIA_SEQ_DECODE_BUDGET: JUMP and the SET_* opcodes are all
// zero-duration and loop straight back to the top of this decode without
// returning, so a track whose jumps cycle with no NOTE/REST/END in between
// would otherwise spin this call forever - fail-safe, silent, exactly like
// running into an unrecognized opcode byte.
static void audio_seq_advance(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];
    uint8_t budget = MIA_SEQ_DECODE_BUDGET;

    while (true) {
        if (seq->cursor >= MIA_RAM_SIZE) {
            audio_seq_finish_track(voice);
            return;
        }

        uint8_t op = mem[seq->cursor];
        uint8_t size = audio_seq_record_size(op);

        if (size == 0) {
            audio_seq_finish_track(voice);
            return;
        }

        if (op == MIA_SEQ_OP_NOTE || op == MIA_SEQ_OP_REST) {
            uint32_t dur = audio_seq_read_duration24(seq->cursor + (op == MIA_SEQ_OP_NOTE ? 3u : 1u));
            if (op == MIA_SEQ_OP_NOTE) {
                audio_seq_apply_note(voice, seq->cursor);
            } else {
                audio_seq_gate(voice, false);
            }
            seq->note_index++;
            seq->event_duration = dur;
            seq->countdown = dur;
            seq->cursor += size;
            return;
        }

        if (op == MIA_SEQ_OP_JUMP) {
            seq->cursor = audio_seq_jump_target(seq->cursor);
        } else {
            audio_seq_apply_config_op(voice, seq->cursor, op);
            seq->cursor += size;
        }

        if (budget-- == 0) {
            audio_seq_finish_track(voice);
            return;
        }
    }
}

// Silently walks forward through events that are entirely in the past,
// bounded to MIA_SEQ_CATCHUP_BUDGET per audio tick so a long VOICE_TAKE
// resolves over a few extra ticks instead of doing unbounded work in one
// sample. Stops and resumes live, in full, on the first event that isn't
// fully covered by the remaining catch-up budget - see docs/audio-sequencer.md.
// This same per-iteration budget also bounds a cyclical run of JUMPs/config
// ops, the same way MIA_SEQ_DECODE_BUDGET does in audio_seq_advance.
static void audio_seq_catchup_step(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];
    uint8_t budget = MIA_SEQ_CATCHUP_BUDGET;

    while (budget-- && seq->catching_up) {
        if (seq->cursor >= MIA_RAM_SIZE) {
            audio_seq_finish_track(voice);
            return;
        }

        uint8_t op = mem[seq->cursor];
        uint8_t size = audio_seq_record_size(op);

        if (size == 0) {
            audio_seq_finish_track(voice);
            return;
        }

        if (op == MIA_SEQ_OP_JUMP) {
            seq->cursor = audio_seq_jump_target(seq->cursor);
            continue;
        }

        if (op == MIA_SEQ_OP_NOTE || op == MIA_SEQ_OP_REST) {
            uint32_t dur = audio_seq_read_duration24(seq->cursor + (op == MIA_SEQ_OP_NOTE ? 3u : 1u));

            if (dur <= seq->catchup_remaining) {
                // Fully in the past: skip it silently, no register writes.
                seq->catchup_remaining -= dur;
                seq->note_index++;
                seq->cursor += size;
                continue;
            }

            // Not fully elapsed: this is the event to resume on, live, in full.
            seq->catching_up = false;
            if (op == MIA_SEQ_OP_NOTE) {
                audio_seq_apply_note(voice, seq->cursor);
            } else {
                audio_seq_gate(voice, false);
            }
            seq->note_index++;
            seq->event_duration = dur;
            seq->countdown = dur;
            seq->cursor += size;
            return;
        }

        // Zero-duration config op crossed while skipping: it's still part of
        // the track's authored state, apply it, then keep scanning.
        audio_seq_apply_config_op(voice, seq->cursor, op);
        seq->cursor += size;
    }
}

static void audio_seq_step(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];

    if (seq->running && !seq->taken) {
        if (seq->catching_up) {
            audio_seq_catchup_step(voice);
        } else if (seq->countdown > 0) {
            seq->countdown--;
        } else {
            audio_seq_advance(voice);
        }
    }

    audio_seq_write_status(voice);
}

static void audio_seq_reset_voice(uint8_t voice) {
    memset(&audio_seq[voice], 0, sizeof(audio_seq[voice]));
    audio_seq[voice].track_base = MIA_SEQ_NO_BASE;
    audio_seq_write_status(voice);
}

// mia_audio_seq_set_base: stores this voice's track base only. Does not
// itself move cursor/note_index - follow with mia_audio_seq_load_track() to
// (re)start decoding from the new base.
void mia_audio_seq_set_base(uint8_t voice, uint32_t base) {
    if (voice >= MIA_AUDIO_VOICE_COUNT) {
        return;
    }

    audio_seq[voice].track_base = base;
}

void mia_audio_seq_load_track(uint8_t voice) {
    if (voice >= MIA_AUDIO_VOICE_COUNT) {
        return;
    }

    audio_seq_t *seq = &audio_seq[voice];
    uint32_t base = seq->track_base;

    memset(seq, 0, sizeof(*seq));
    seq->track_base = base;
    seq->cursor = base;

    audio_seq_write_status(voice);
}

static void audio_seq_start_one(uint8_t voice) {
    // A voice no program has given a track stays stopped - no gate-off and
    // no IRQ_AUDIO_SEQ_DONE, exactly as if it weren't in the mask.
    if (audio_seq[voice].track_base == MIA_SEQ_NO_BASE) {
        return;
    }

    audio_seq[voice].running = true;
    audio_seq[voice].taken = false;
    audio_seq[voice].catching_up = false;
}

void mia_audio_seq_start(uint8_t voice_mask) {
    for (uint8_t v = 0; v < MIA_AUDIO_VOICE_COUNT; v++) {
        if (voice_mask & (1u << v)) {
            audio_seq_start_one(v);
        }
    }
}

void mia_audio_seq_stop(uint8_t voice_mask) {
    for (uint8_t v = 0; v < MIA_AUDIO_VOICE_COUNT; v++) {
        if (voice_mask & (1u << v)) {
            audio_seq[v].running = false;
            audio_seq[v].taken = false;
            audio_seq[v].catching_up = false;
            audio_seq_gate(v, false);
            audio_seq_write_status(v);
        }
    }
}

void mia_audio_voice_take(uint8_t voice_mask) {
    for (uint8_t v = 0; v < MIA_AUDIO_VOICE_COUNT; v++) {
        if (voice_mask & (1u << v)) {
            audio_seq[v].taken = true;
            audio_seq[v].taken_at = audio_seq_clock;
            audio_seq_write_status(v);
        }
    }
}

// See docs/audio-sequencer.md, "Catch-up (VOICE_RELEASE reconciliation)" for
// the derivation: already_spent credits however far into the current event
// this voice was when taken, so a short TAKE resumes the same event without
// retriggering it, and a long one skips forward by exactly the real elapsed
// time - not just the time since VOICE_TAKE.
static void audio_seq_release_one(uint8_t voice) {
    audio_seq_t *seq = &audio_seq[voice];

    if (!seq->taken) {
        return;
    }
    seq->taken = false;

    if (!seq->running) {
        audio_seq_write_status(voice);
        return;
    }

    uint32_t elapsed_since_take = audio_seq_clock - seq->taken_at;
    uint32_t already_spent = seq->event_duration - seq->countdown;
    uint32_t total_elapsed = already_spent + elapsed_since_take;

    if (total_elapsed < seq->event_duration) {
        // Real time hasn't reached this event's natural end yet: resume the
        // same event mid-flight, no retrigger.
        seq->countdown = seq->event_duration - total_elapsed;
    } else {
        // This event is over: the next audio tick's catch-up scan walks
        // forward from here using the leftover budget.
        seq->catching_up = true;
        seq->catchup_remaining = total_elapsed - seq->event_duration;
    }

    audio_seq_write_status(voice);
}

void mia_audio_voice_release(uint8_t voice_mask) {
    for (uint8_t v = 0; v < MIA_AUDIO_VOICE_COUNT; v++) {
        if (voice_mask & (1u << v)) {
            audio_seq_release_one(v);
        }
    }
}

void mia_audio_seq_write_test_track(uint8_t voice, uint32_t base) {
    if (voice >= MIA_AUDIO_VOICE_COUNT) {
        return;
    }

    // attack=0/decay=3/sustain=F/release=5, then C4/E4/G4 at ~440 Hz's
    // neighbors (6000 samples/~0.25s each), then JUMP -25 back to offset 0
    // (the SET_ADSR) to loop the whole body forever. -25 as a signed 24-bit
    // little-endian offset is 0xFFFFE7 (0xE7, 0xFF, 0xFF).
    static const uint8_t kTestTrack[] = {
        MIA_SEQ_OP_SET_ADSR, 0x03, 0xF5,
        MIA_SEQ_OP_NOTE, 0x5A, 0x10, 0x70, 0x17, 0x00,   // C4
        MIA_SEQ_OP_NOTE, 0x9A, 0x14, 0x70, 0x17, 0x00,   // E4
        MIA_SEQ_OP_NOTE, 0x80, 0x18, 0x70, 0x17, 0x00,   // G4
        MIA_SEQ_OP_JUMP, 0xE7, 0xFF, 0xFF,
    };

    if (base > MIA_RAM_SIZE - sizeof(kTestTrack)) {
        return;
    }
    for (size_t i = 0; i < sizeof(kTestTrack); i++) {
        mem[base + i] = kTestTrack[i];
    }
    mia_audio_seq_set_base(voice, base);
}

void mia_audio_seq_print_status(void) {
    printf("Sequencer:\n");
    for (uint8_t v = 0; v < MIA_AUDIO_VOICE_COUNT; v++) {
        audio_seq_t *seq = &audio_seq[v];
        if (seq->track_base == MIA_SEQ_NO_BASE) {
            printf("  ch%u: no track\n", v);
            continue;
        }
        printf("  ch%u: %s%s%s  note:%u  base:$%05X  cursor:$%05X  countdown:%u\n",
               v,
               seq->running ? "running" : "stopped",
               seq->taken ? " taken" : "",
               seq->catching_up ? " catching-up" : "",
               (unsigned)seq->note_index,
               (unsigned)seq->track_base,
               (unsigned)seq->cursor,
               (unsigned)seq->countdown);
    }
}

static void __isr __attribute__((optimize("O3")))
__time_critical_func(audio_irq_handler)(void) {
    pwm_clear_irq(AUDIO_IRQ_SLICE);

    audio_seq_clock++;
    audio_drain_queue(16);

    for (uint8_t i = 0; i < MIA_AUDIO_VOICE_COUNT; i++) {
        audio_seq_step(i);
    }

    int16_t sample_l = 0;
    int16_t sample_r = 0;

    for (uint8_t i = 0; i < MIA_AUDIO_VOICE_COUNT; i++) {
        audio_voice_t *voice = &audio_voices[i];

        int16_t sample = audio_next_sample(voice);
        audio_update_envelope(voice);
        sample = ((int32_t)sample * (int32_t)(voice->vol >> 16)) >> 8;
        sample = ((int32_t)sample * (int32_t)voice->volume) >> 8;

        sample_l += ((int32_t)sample * voice->pan_l) >> 7;
        sample_r += ((int32_t)sample * voice->pan_r) >> 7;
    }

    uint16_t master_gain = audio_master_gain();
    sample_l = (int16_t)(((int32_t)sample_l * master_gain) >> 8);
    sample_r = (int16_t)(((int32_t)sample_r * master_gain) >> 8);

    int16_t max_val = (1 << (AUDIO_PWM_BITS - 1)) - 1;
    int16_t min_val = -(1 << (AUDIO_PWM_BITS - 1));

    if (sample_l < min_val) sample_l = min_val;
    if (sample_l > max_val) sample_l = max_val;
    if (sample_r < min_val) sample_r = min_val;
    if (sample_r > max_val) sample_r = max_val;

    pwm_set_chan_level(AUDIO_L_SLICE, AUDIO_L_CHAN, (uint16_t)(sample_l + AUDIO_PWM_CENTER));
    pwm_set_chan_level(AUDIO_R_SLICE, AUDIO_R_CHAN, (uint16_t)(sample_r + AUDIO_PWM_CENTER));
}

static void audio_set_irq_rate(void) {
    uint32_t wrap = clock_get_hz(clk_sys) / MIA_AUDIO_SAMPLE_RATE;
    if (wrap == 0) {
        wrap = 1;
    }
    if (wrap > 65536u) {
        wrap = 65536u;
    }
    pwm_set_wrap(AUDIO_IRQ_SLICE, wrap - 1u);
}

static void audio_set_pwm_center(void) {
    pwm_set_chan_level(AUDIO_L_SLICE, AUDIO_L_CHAN, AUDIO_PWM_CENTER);
    pwm_set_chan_level(AUDIO_R_SLICE, AUDIO_R_CHAN, AUDIO_PWM_CENTER);
}

void mia_audio_init(void) {
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, ((1u << AUDIO_PWM_BITS) - 1u));

    pwm_init(AUDIO_L_SLICE, &config, true);
    if (AUDIO_R_SLICE != AUDIO_L_SLICE) {
        pwm_init(AUDIO_R_SLICE, &config, true);
    }

    pwm_config irq_config = pwm_get_default_config();
    pwm_init(AUDIO_IRQ_SLICE, &irq_config, false);

    audio_set_pwm_center();

    gpio_set_drive_strength(MIA_AUDIO_L_PIN, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_drive_strength(MIA_AUDIO_R_PIN, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(MIA_AUDIO_L_PIN, GPIO_SLEW_RATE_SLOW);
    gpio_set_slew_rate(MIA_AUDIO_R_PIN, GPIO_SLEW_RATE_SLOW);
    gpio_disable_pulls(MIA_AUDIO_L_PIN);
    gpio_disable_pulls(MIA_AUDIO_R_PIN);
    gpio_set_function(MIA_AUDIO_L_PIN, GPIO_FUNC_PWM);
    gpio_set_function(MIA_AUDIO_R_PIN, GPIO_FUNC_PWM);

    for (uint32_t i = 0; i < 256u; i++) {
        audio_sine_table[i] = (int8_t)lround(sin((M_PI * 2.0 / 256.0) * (double)i) * 127.0);
    }

    irq_set_priority(PWM_IRQ_WRAP_0, PICO_DEFAULT_IRQ_PRIORITY + 0x10);
    mia_audio_reset_runtime_state();
}

void mia_audio_reclock(void) {
    if (!audio_active) {
        return;
    }

    irq_set_enabled(PWM_IRQ_WRAP_0, false);
    audio_set_irq_rate();
    pwm_clear_irq(AUDIO_IRQ_SLICE);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
}

void mia_audio_enable(void) {
    irq_set_enabled(PWM_IRQ_WRAP_0, false);
    pwm_set_irq_enabled(AUDIO_IRQ_SLICE, false);

    audio_queue_head = 0;
    audio_queue_tail = 0;
    audio_queue_overflow = false;
    audio_sync_from_memory();

    audio_set_irq_rate();
    pwm_clear_irq(AUDIO_IRQ_SLICE);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, audio_irq_handler);
    pwm_set_irq_enabled(AUDIO_IRQ_SLICE, true);
    audio_active = true;
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_STATUS] =
        (mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_STATUS] & (uint8_t)~MIA_AUDIO_STATUS_QUEUE_OVERFLOW) |
        MIA_AUDIO_STATUS_ACTIVE;
    mia_status_set_flag(MIA_STAT_AUDIO_ACTIVE);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
}

void mia_audio_stop(void) {
    irq_set_enabled(PWM_IRQ_WRAP_0, false);
    pwm_set_irq_enabled(AUDIO_IRQ_SLICE, false);
    pwm_clear_irq(AUDIO_IRQ_SLICE);

    audio_active = false;
    audio_set_pwm_center();
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_STATUS] &= (uint8_t)~MIA_AUDIO_STATUS_ACTIVE;
    mia_status_clear_flag(MIA_STAT_AUDIO_ACTIVE);
}

void mia_audio_reset(void) {
    mia_audio_stop();
    mia_audio_reset_runtime_state();
}

void mia_audio_reset_runtime_state(void) {
    mia_audio_stop();

    memset(&mem[MIA_AUDIO_STATE_OFFSET], 0, MIA_AUDIO_STATE_SIZE);
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_VERSION] = MIA_AUDIO_VERSION;
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_VOLUME] = MIA_AUDIO_MASTER_VOLUME_DEFAULT;
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_CHANNELS] = MIA_AUDIO_VOICE_COUNT;
    audio_write_u16(MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_RATE_L, MIA_AUDIO_SAMPLE_RATE);
    mem[MIA_AUDIO_HEADER_OFFSET + MIA_AUDIO_HEADER_FLAGS] = MIA_AUDIO_FLAG_STEREO;
    audio_master_volume = MIA_AUDIO_MASTER_VOLUME_DEFAULT;

    for (uint8_t voice = 0; voice < MIA_AUDIO_VOICE_COUNT; voice++) {
        uint32_t base = audio_voice_base(voice);
        mem[base + MIA_AUDIO_VOICE_PULSE_WIDTH] = 128;
        mem[base + MIA_AUDIO_VOICE_SUSTAIN_RELEASE] = 0xF5;
        mem[base + MIA_AUDIO_VOICE_WAVEFORM] = MIA_AUDIO_WAVE_PULSE;
        mem[base + MIA_AUDIO_VOICE_PAN] = 0;
        mem[base + MIA_AUDIO_VOICE_VOLUME] = MIA_AUDIO_VOICE_VOLUME_DEFAULT;
        audio_reset_voice_state(voice);
    }

    audio_queue_head = 0;
    audio_queue_tail = 0;
    audio_queue_overflow = false;

    // Background sequencer: forget every voice's track. The track bytes stay
    // wherever the program put them - nothing in MIA RAM belongs to the
    // sequencer (see docs/audio-sequencer.md).
    for (uint8_t voice = 0; voice < MIA_AUDIO_VOICE_COUNT; voice++) {
        audio_seq_reset_voice(voice);
    }

    audio_configure_indexes();
}

void __not_in_flash_func(mia_audio_core1_on_write)(uint32_t offset, uint8_t value) {
    if (!audio_active ||
        offset < MIA_AUDIO_STATE_OFFSET ||
        offset >= MIA_AUDIO_STATE_OFFSET + MIA_AUDIO_STATE_SIZE) {
        return;
    }

    uint8_t next = (audio_queue_head + 1u) & (AUDIO_QUEUE_SIZE - 1u);
    if (next == audio_queue_tail) {
        audio_queue_overflow = true;
        error_defer(ERROR_DEFER_AUDIO_QUEUE_OVERFLOW);
        return;
    }

    uint8_t head = audio_queue_head;
    audio_queue[head].loc = (uint8_t)(offset - MIA_AUDIO_STATE_OFFSET);
    audio_queue[head].value = value;
    __dmb();
    audio_queue_head = next;
}

bool mia_audio_is_active(void) {
    return audio_active;
}

void mia_audio_print_summary(void) {
    printf("  Audio:  %s  %u Hz  %u voices  stereo GPIO %u/%u\n",
           audio_active ? "active" : "stopped",
           MIA_AUDIO_SAMPLE_RATE,
           MIA_AUDIO_VOICE_COUNT,
           MIA_AUDIO_L_PIN,
           MIA_AUDIO_R_PIN);
}

void mia_audio_print_status(void) {
    printf("Audio:\n");
    printf("  state:     %s\n", audio_active ? "active" : "stopped");
    printf("  rate:      %u Hz\n", MIA_AUDIO_SAMPLE_RATE);
    printf("  voices:    %u\n", MIA_AUDIO_VOICE_COUNT);
    printf("  volume:    %u/15 (master)\n", audio_master_volume & MIA_AUDIO_MASTER_VOLUME_MAX);
    printf("  pins:      L GPIO%u  R GPIO%u  IRQ-slice GPIO%u\n",
           MIA_AUDIO_L_PIN, MIA_AUDIO_R_PIN, MIA_AUDIO_IRQ_PIN);
    printf("  block:     $%05X-$%05X\n",
           MIA_AUDIO_STATE_OFFSET,
           MIA_AUDIO_STATE_OFFSET + MIA_AUDIO_STATE_SIZE - 1u);
    printf("  indexes:   all:$%02X  ch0:$%02X ch1:$%02X ch2:$%02X ch3:$%02X  header:$%02X\n",
           MIA_AUDIO_INDEX_ALL,
           MIA_AUDIO_INDEX_VOICE0,
           MIA_AUDIO_INDEX_VOICE1,
           MIA_AUDIO_INDEX_VOICE2,
           MIA_AUDIO_INDEX_VOICE3,
           MIA_AUDIO_INDEX_HEADER);
    printf("  queue:     head:%u tail:%u overflow:%s\n",
           (unsigned)audio_queue_head,
           (unsigned)audio_queue_tail,
           audio_queue_overflow ? "yes" : "no");

    for (uint8_t voice = 0; voice < MIA_AUDIO_VOICE_COUNT; voice++) {
        uint32_t base = audio_voice_base(voice);
        uint16_t freq_q4 = audio_read_u16(base + MIA_AUDIO_VOICE_FREQ_L);
        uint8_t attack_decay = mem[base + MIA_AUDIO_VOICE_ATTACK_DECAY];
        uint8_t sustain_release = mem[base + MIA_AUDIO_VOICE_SUSTAIN_RELEASE];
        uint8_t control = mem[base + MIA_AUDIO_VOICE_CONTROL];

        printf("  ch%u:      freq:%u.%u Hz  wave:%u  pulse:%u  vol:%u  pan:%d  AD:%X/%X  SR:%X/%X  gate:%s\n",
               voice,
               freq_q4 >> 4,
               (unsigned)((freq_q4 & 0x0Fu) * 10u / 16u),
               (unsigned)mem[base + MIA_AUDIO_VOICE_WAVEFORM],
               (unsigned)mem[base + MIA_AUDIO_VOICE_PULSE_WIDTH],
               (unsigned)mem[base + MIA_AUDIO_VOICE_VOLUME],
               (int8_t)mem[base + MIA_AUDIO_VOICE_PAN],
               attack_decay >> 4,
               attack_decay & 0x0Fu,
               sustain_release >> 4,
               sustain_release & 0x0Fu,
               (control & MIA_AUDIO_CONTROL_GATE) ? "on" : "off");
    }
}

static void audio_configure_indexes(void) {
    audio_configure_index(MIA_AUDIO_INDEX_ALL, MIA_AUDIO_STATE_OFFSET, MIA_AUDIO_STATE_SIZE);
    audio_configure_index(MIA_AUDIO_INDEX_VOICE0, MIA_AUDIO_VOICE_OFFSET(0), MIA_AUDIO_VOICE_SIZE);
    audio_configure_index(MIA_AUDIO_INDEX_VOICE1, MIA_AUDIO_VOICE_OFFSET(1), MIA_AUDIO_VOICE_SIZE);
    audio_configure_index(MIA_AUDIO_INDEX_VOICE2, MIA_AUDIO_VOICE_OFFSET(2), MIA_AUDIO_VOICE_SIZE);
    audio_configure_index(MIA_AUDIO_INDEX_VOICE3, MIA_AUDIO_VOICE_OFFSET(3), MIA_AUDIO_VOICE_SIZE);
    audio_configure_index(MIA_AUDIO_INDEX_HEADER, MIA_AUDIO_HEADER_OFFSET, MIA_AUDIO_HEADER_SIZE);

    // Sequencer status: parked at voice offset $09 so PLAYING(v)/CUE(v) can
    // poll SEQ_NOTE_INDEX_L/H and SEQ_STATUS without stepping through the
    // rest of the record first. 3 bytes, wraps back to $09 after $0B.
    audio_configure_index(MIA_AUDIO_INDEX_SEQ_VOICE0,
                           audio_voice_base(0) + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L, 3u);
    audio_configure_index(MIA_AUDIO_INDEX_SEQ_VOICE1,
                           audio_voice_base(1) + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L, 3u);
    audio_configure_index(MIA_AUDIO_INDEX_SEQ_VOICE2,
                           audio_voice_base(2) + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L, 3u);
    audio_configure_index(MIA_AUDIO_INDEX_SEQ_VOICE3,
                           audio_voice_base(3) + MIA_AUDIO_VOICE_SEQ_NOTE_INDEX_L, 3u);
}

static void audio_configure_index(uint8_t index_id, uint32_t start, uint32_t length) {
    idx[index_id].current_addr = start;
    idx[index_id].default_addr = start;
    idx[index_id].limit_addr = start + length;
    idx[index_id].step = 1u;
    idx[index_id].flags = (1u << IDX_FLAG_R_STP_ENA) |
                          (1u << IDX_FLAG_W_STP_ENA) |
                          (1u << IDX_FLAG_WRAP_ENA);
    idx[index_id].reserved = 0;
}
