#ifndef _MIA_AUDIO_AUDIO_H_
#define _MIA_AUDIO_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>

#define MIA_AUDIO_SAMPLE_RATE 24000u
#define MIA_AUDIO_VOICE_COUNT 4u
#define MIA_AUDIO_VOICE_SIZE 8u

#define MIA_AUDIO_STATE_OFFSET 0x12000u
#define MIA_AUDIO_STATE_SIZE 0x40u
#define MIA_AUDIO_HEADER_OFFSET MIA_AUDIO_STATE_OFFSET
#define MIA_AUDIO_HEADER_SIZE 0x10u
#define MIA_AUDIO_VOICES_OFFSET (MIA_AUDIO_STATE_OFFSET + MIA_AUDIO_HEADER_SIZE)
#define MIA_AUDIO_VOICE_OFFSET(voice) (MIA_AUDIO_VOICES_OFFSET + (voice) * MIA_AUDIO_VOICE_SIZE)

#define MIA_AUDIO_VERSION 1u

#define MIA_AUDIO_HEADER_VERSION 0x00u
#define MIA_AUDIO_HEADER_CONTROL 0x01u
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

#define MIA_AUDIO_WAVE_SINE 0x00u
#define MIA_AUDIO_WAVE_PULSE 0x01u
#define MIA_AUDIO_WAVE_SAW 0x02u
#define MIA_AUDIO_WAVE_TRIANGLE 0x03u
#define MIA_AUDIO_WAVE_NOISE 0x04u

#define MIA_AUDIO_CONTROL_GATE (1u << 0)
#define MIA_AUDIO_CONTROL_RESET_PHASE (1u << 1)

#define MIA_AUDIO_INDEX_ALL 0xD0u
#define MIA_AUDIO_INDEX_VOICE0 0xD1u
#define MIA_AUDIO_INDEX_VOICE1 0xD2u
#define MIA_AUDIO_INDEX_VOICE2 0xD3u
#define MIA_AUDIO_INDEX_VOICE3 0xD4u
#define MIA_AUDIO_INDEX_HEADER 0xD5u

#define MIA_CMD_AUDIO_ENABLE 0x60u
#define MIA_CMD_AUDIO_STOP 0x61u
#define MIA_CMD_AUDIO_RESET 0x62u

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

#endif
