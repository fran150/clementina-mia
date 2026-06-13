#include "input_internal.h"

#include "hardware/sync.h"

#define INPUT_FIFO_MASK (MIA_INPUT_TEXT_FIFO_SIZE - 1u)

static uint8_t text_fifo[MIA_INPUT_TEXT_FIFO_SIZE];
static volatile uint32_t text_head_seq;
static volatile uint32_t text_tail_seq;

static inline uint32_t input_fifo_effective_tail(uint32_t head, uint32_t tail) {
    uint32_t count = head - tail;
    if (count > MIA_INPUT_TEXT_FIFO_SIZE) {
        return head - MIA_INPUT_TEXT_FIFO_SIZE;
    }
    return tail;
}

static inline uint8_t input_fifo_count_for_register(uint32_t head, uint32_t tail) {
    uint32_t count = head - input_fifo_effective_tail(head, tail);
    if (count > MIA_INPUT_TEXT_FIFO_SIZE) {
        return MIA_INPUT_TEXT_FIFO_SIZE;
    }
    return (uint8_t)count;
}

static inline uint8_t input_fifo_index(uint32_t sequence) {
    return (uint8_t)(sequence & INPUT_FIFO_MASK);
}

void input_publish_text_snapshot(void) {
    uint32_t head = text_head_seq;
    __dmb();
    uint32_t tail = input_fifo_effective_tail(head, text_tail_seq);
    uint8_t count = input_fifo_count_for_register(head, tail);

    cached_input_char_count = count;
    cached_input_char = count == 0 ? 0 : text_fifo[input_fifo_index(tail)];
    if (count == 0) {
        cached_input_status &= (uint8_t)~INPUT_TEXT_READY;
    } else {
        cached_input_status |= INPUT_TEXT_READY;
    }
    input_publish_registers();
}

void mia_input_core1_on_char_read(void) {
    uint32_t head = text_head_seq;
    uint32_t tail = input_fifo_effective_tail(head, text_tail_seq);

    if (head != tail) {
        text_tail_seq = tail + 1u;
        __dmb();
    }

    input_publish_text_snapshot();
}

void input_enqueue_text(uint8_t value) {
    uint32_t head = text_head_seq;

    text_fifo[input_fifo_index(head)] = value;
    __dmb();
    text_head_seq = head + 1u;

    input_publish_text_snapshot();

    input_set_keyboard_events(KEY_EVENT_TEXT);
}

void input_clear_text_fifo(void) {
    text_tail_seq = text_head_seq;
    __dmb();
    input_publish_text_snapshot();
}
