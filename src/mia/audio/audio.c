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

static int8_t audio_sine_table[256];
static audio_voice_t audio_voices[MIA_AUDIO_VOICE_COUNT];
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

static void __isr __attribute__((optimize("O3")))
__time_critical_func(audio_irq_handler)(void) {
    pwm_clear_irq(AUDIO_IRQ_SLICE);

    audio_drain_queue(16);

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
