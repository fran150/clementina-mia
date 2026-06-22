#include "input_internal.h"

#include <string.h>

#include "pico/time.h"

#include "irq/irq.h"

bool input_bitmap_any(const uint8_t *bitmap) {
    for (uint32_t i = 0; i < 32u; i++) {
        if (bitmap[i] != 0) {
            return true;
        }
    }
    return false;
}

bool input_gamepad_any_digital_down(void) {
    for (uint8_t i = 0; i < 4u; i++) {
        const uint8_t *slot = input_gamepad_slot(i);
        if ((slot[0] & 0x80u) == 0) {
            continue;
        }
        if ((slot[0] & 0x0Fu) != 0 || slot[1] != 0 || slot[2] != 0 || slot[3] != 0) {
            return true;
        }
    }
    return false;
}

void input_set_keyboard_events(uint8_t flags) {
    if (flags == 0) {
        return;
    }
    mem[KEYBOARD_EVENT_FLAGS_OFFSET] |= flags;
    input_update_irqs();
}

void input_set_mouse_events(uint8_t flags) {
    if (flags == 0) {
        return;
    }
    mem[MOUSE_EVENT_FLAGS_OFFSET] |= flags;
    input_update_irqs();
}

void input_set_gamepad_events(uint8_t flags) {
    if (flags == 0) {
        return;
    }
    mem[GAMEPAD_EVENT_FLAGS_OFFSET] |= flags;
    input_update_irqs();
}

void input_set_device_flags(uint8_t flags) {
    uint8_t old = mem[INPUT_DEVICE_FLAGS_OFFSET];
    flags &= 0x7Fu;
    if (old == flags) {
        return;
    }

    uint8_t changed = old ^ flags;
    mem[INPUT_DEVICE_FLAGS_OFFSET] = flags;

    if ((changed & (INPUT_DEVICE_KEYBOARD | INPUT_DEVICE_CONSUMER)) != 0) {
        input_set_keyboard_events(KEY_EVENT_DEVICE);
    }
    if ((changed & INPUT_DEVICE_MOUSE) != 0) {
        input_set_mouse_events(MOUSE_EVENT_DEVICE);
    }
    if ((changed & (INPUT_DEVICE_GAMEPAD_0 | INPUT_DEVICE_GAMEPAD_1 | INPUT_DEVICE_GAMEPAD_2 | INPUT_DEVICE_GAMEPAD_3)) != 0) {
        input_set_gamepad_events(GAMEPAD_EVENT_DEVICE);
    }
}

void input_update_irqs(void) {
    if ((mem[KEYBOARD_EVENT_FLAGS_OFFSET] & mem[KEYBOARD_EVENT_MASK_OFFSET]) != 0) {
        mia_irq_set_flag(IRQ_INPUT_KEYBOARD);
    }
    if ((mem[MOUSE_EVENT_FLAGS_OFFSET] & mem[MOUSE_EVENT_MASK_OFFSET]) != 0) {
        mia_irq_set_flag(IRQ_INPUT_MOUSE);
    }
    if ((mem[GAMEPAD_EVENT_FLAGS_OFFSET] & mem[GAMEPAD_EVENT_MASK_OFFSET]) != 0) {
        mia_irq_set_flag(IRQ_INPUT_GAMEPAD);
    }
}

void input_apply_event_acks(void) {
    uint8_t ack = mem[KEYBOARD_EVENT_ACK_OFFSET];
    if (ack != 0) {
        mem[KEYBOARD_EVENT_FLAGS_OFFSET] &= (uint8_t)~ack;
        mem[KEYBOARD_EVENT_ACK_OFFSET] = 0;
    }

    ack = mem[MOUSE_EVENT_ACK_OFFSET];
    if (ack != 0) {
        mem[MOUSE_EVENT_FLAGS_OFFSET] &= (uint8_t)~ack;
        mem[MOUSE_EVENT_ACK_OFFSET] = 0;
    }

    ack = mem[GAMEPAD_EVENT_ACK_OFFSET];
    if (ack != 0) {
        mem[GAMEPAD_EVENT_FLAGS_OFFSET] &= (uint8_t)~ack;
        mem[GAMEPAD_EVENT_ACK_OFFSET] = 0;
    }

    input_update_irqs();
}

void input_clear_keyboard_consumer(bool publish_events) {
    uint8_t key_events = 0;

    if (publish_events) {
        if (input_bitmap_any(input_keyboard_bitmap())) {
            key_events |= KEY_EVENT_KEY_UP;
        }
        if (input_bitmap_any(input_consumer_bitmap())) {
            key_events |= KEY_EVENT_CONSUMER_UP;
        }
    }

    memset(input_keyboard_bitmap(), 0, 32u);
    memset(input_consumer_bitmap(), 0, 32u);

    input_set_keyboard_events(key_events);
}

void input_clear_mouse(bool publish_events) {
    uint8_t *mouse = input_mouse_state();
    uint8_t events = 0;

    if (publish_events) {
        if ((mouse[0] & MOUSE_BUTTON_MASK) != 0) {
            events |= MOUSE_EVENT_BUTTON_UP;
        }
        if (mouse[1] != 0 || mouse[2] != 0) {
            events |= MOUSE_EVENT_MOVE;
        }
        if (mouse[3] != 0 || mouse[4] != 0) {
            events |= MOUSE_EVENT_SCROLL;
        }
    }

    memset(mouse, 0, 5u);
    input_set_mouse_events(events);
}

static void input_update_gamepad_device_flag(uint8_t player, bool connected) {
    uint8_t mask = (uint8_t)(INPUT_DEVICE_GAMEPAD_0 << player);
    uint8_t flags = mem[INPUT_DEVICE_FLAGS_OFFSET];
    if (connected) {
        flags |= mask;
    } else {
        flags &= (uint8_t)~mask;
    }
    input_set_device_flags(flags);
}

void input_clear_gamepad_slot(uint8_t player, bool publish_events) {
    uint8_t *slot = input_gamepad_slot(player);
    uint8_t events = 0;

    if (publish_events) {
        if ((slot[2] | slot[3]) != 0) {
            events |= GAMEPAD_EVENT_BUTTON_UP;
        }
        if ((slot[0] & 0x0Fu) != 0) {
            events |= GAMEPAD_EVENT_DPAD;
        }
        if (slot[1] != 0 || slot[4] != 0 || slot[5] != 0 || slot[6] != 0 || slot[7] != 0) {
            events |= GAMEPAD_EVENT_STICK;
        }
        if (slot[8] != 0 || slot[9] != 0) {
            events |= GAMEPAD_EVENT_TRIGGER;
        }
    }

    memset(slot, 0, GAMEPAD_SLOT_SIZE);
    input_update_gamepad_device_flag(player, false);
    input_set_gamepad_events(events);
}

void input_clear_gamepads(bool publish_events) {
    for (uint8_t i = 0; i < 4u; i++) {
        input_clear_gamepad_slot(i, publish_events);
    }
}

void input_clear_live_state(bool publish_events) {
    input_clear_keyboard_consumer(publish_events);
    input_clear_mouse(publish_events);
    input_clear_gamepads(publish_events);
    input_set_device_flags(0);
    input_recompute_status();
}

uint8_t input_decode_key_usage(uint16_t usage_id) {
    // Maps a keyboard HID usage to the control byte MIA pushes into the text FIFO
    // for non-text editing keys: cursor moves, Home, and the editing control
    // keys. Returns 0 for everything else - printable characters reach the FIFO
    // as text instead, so they must not decode here or they would be enqueued
    // twice. MIA owns this table (rather than the input client) so the keyboard
    // decode lives in one place, mirroring how the C64 KERNAL, not the keyboard,
    // owns the decode table. Cursor and Home codes use PETSCII values; the
    // control keys reuse their ASCII codes.
    switch (usage_id) {
    case 0x28u: // Enter
    case 0x58u: // Keypad Enter
        return 0x0Du;
    case 0x2Bu: // Tab
        return 0x09u;
    case 0x2Au: // Backspace
        return 0x08u;
    case 0x29u: // Escape
        return 0x1Bu;
    case 0x49u: // Insert
        return 0x94u;
    case 0x4Au: // Home
        return 0x13u;
    case 0x4Cu: // Delete (forward)
        return 0x7Fu;
    case 0x4Fu: // Right Arrow
        return 0x1Du;
    case 0x50u: // Left Arrow
        return 0x9Du;
    case 0x51u: // Down Arrow
        return 0x11u;
    case 0x52u: // Up Arrow
        return 0x91u;
    default:
        return 0u;
    }
}

// Key auto-repeat. While repeat_usage is held, repeat_byte is re-enqueued into
// the text FIFO after an initial delay, then at a steady interval, matching a
// typewriter-style repeat. repeat_usage == 0 means nothing is repeating.
#define KEY_REPEAT_DELAY_US    400000u // wait before the first repeat
#define KEY_REPEAT_INTERVAL_US 60000u  // ~16 repeats/sec while held

static uint16_t repeat_usage = 0;
static uint8_t repeat_byte = 0;
static uint64_t repeat_deadline_us = 0;

bool input_key_repeats(uint16_t usage_id) {
    // Only the keys where holding is useful repeat (cursor moves and Backspace);
    // Enter and the other one-shot keys fire once per press.
    switch (usage_id) {
    case 0x2Au: // Backspace
    case 0x4Cu: // Delete (forward)
    case 0x4Fu: // Right Arrow
    case 0x50u: // Left Arrow
    case 0x51u: // Down Arrow
    case 0x52u: // Up Arrow
        return true;
    default:
        return false;
    }
}

static bool input_usage_down(uint16_t usage_id) {
    if (usage_id > 0x00FFu) {
        return false;
    }
    const uint8_t *bitmap = input_keyboard_bitmap();
    return (bitmap[usage_id >> 3] & (uint8_t)(1u << (usage_id & 7u))) != 0;
}

void input_repeat_arm(uint16_t usage_id, uint8_t byte) {
    repeat_usage = usage_id;
    repeat_byte = byte;
    repeat_deadline_us = time_us_64() + KEY_REPEAT_DELAY_US;
}

void input_repeat_release(uint16_t usage_id) {
    if (usage_id == repeat_usage) {
        repeat_usage = 0;
    }
}

void input_repeat_service(void) {
    if (repeat_usage == 0) {
        return;
    }
    if (!input_usage_down(repeat_usage)) {
        // Released without going through the HID-event path (e.g. bitmap replace).
        repeat_usage = 0;
        return;
    }

    uint64_t now = time_us_64();
    if (now < repeat_deadline_us) {
        return;
    }

    input_enqueue_text(repeat_byte);
    input_recompute_status();
    repeat_deadline_us = now + KEY_REPEAT_INTERVAL_US;
}

void input_set_hid_usage(uint16_t usage_page, uint16_t usage_id, bool down) {
    if (usage_id > 0x00FFu) {
        return;
    }

    uint8_t *bitmap;
    uint8_t down_event;
    uint8_t up_event;

    if (usage_page == HID_PAGE_KEYBOARD) {
        bitmap = input_keyboard_bitmap();
        down_event = KEY_EVENT_KEY_DOWN;
        up_event = KEY_EVENT_KEY_UP;
    } else if (usage_page == HID_PAGE_CONSUMER) {
        bitmap = input_consumer_bitmap();
        down_event = KEY_EVENT_CONSUMER_DOWN;
        up_event = KEY_EVENT_CONSUMER_UP;
    } else {
        return;
    }

    uint8_t byte_i = (uint8_t)(usage_id >> 3);
    uint8_t mask = (uint8_t)(1u << (usage_id & 7u));
    bool was_down = (bitmap[byte_i] & mask) != 0;

    if (down) {
        bitmap[byte_i] |= mask;
    } else {
        bitmap[byte_i] &= (uint8_t)~mask;
    }

    if (was_down != down) {
        input_set_keyboard_events(down ? down_event : up_event);
        input_recompute_status();
    }
}

void input_set_hid_bitmap(uint16_t usage_page, const uint8_t *new_bitmap) {
    uint8_t *bitmap;
    uint8_t down_event;
    uint8_t up_event;

    if (usage_page == HID_PAGE_KEYBOARD) {
        bitmap = input_keyboard_bitmap();
        down_event = KEY_EVENT_KEY_DOWN;
        up_event = KEY_EVENT_KEY_UP;
    } else if (usage_page == HID_PAGE_CONSUMER) {
        bitmap = input_consumer_bitmap();
        down_event = KEY_EVENT_CONSUMER_DOWN;
        up_event = KEY_EVENT_CONSUMER_UP;
    } else {
        return;
    }

    uint8_t events = 0;
    for (uint8_t i = 0; i < 32u; i++) {
        uint8_t old = bitmap[i];
        uint8_t next = new_bitmap[i];
        uint8_t changed = old ^ next;
        if ((changed & next) != 0) {
            events |= down_event;
        }
        if ((changed & old) != 0) {
            events |= up_event;
        }
        bitmap[i] = next;
    }

    input_set_keyboard_events(events);
    input_recompute_status();
}

void input_apply_mouse_delta(const uint8_t *payload) {
    uint8_t *mouse = input_mouse_state();
    uint8_t old_buttons = mouse[0] & MOUSE_BUTTON_MASK;
    uint8_t new_buttons = payload[0] & MOUSE_BUTTON_MASK;
    uint8_t changed = old_buttons ^ new_buttons;
    uint8_t events = 0;

    if ((changed & new_buttons) != 0) {
        events |= MOUSE_EVENT_BUTTON_DOWN;
    }
    if ((changed & old_buttons) != 0) {
        events |= MOUSE_EVENT_BUTTON_UP;
    }

    mouse[0] = new_buttons;
    mouse[1] = (uint8_t)(mouse[1] + (uint8_t)(int8_t)payload[1]);
    mouse[2] = (uint8_t)(mouse[2] + (uint8_t)(int8_t)payload[2]);
    mouse[3] = (uint8_t)(mouse[3] + (uint8_t)(int8_t)payload[3]);
    mouse[4] = (uint8_t)(mouse[4] + (uint8_t)(int8_t)payload[4]);

    if (payload[1] != 0 || payload[2] != 0) {
        events |= MOUSE_EVENT_MOVE;
    }
    if (payload[3] != 0 || payload[4] != 0) {
        events |= MOUSE_EVENT_SCROLL;
    }

    input_set_mouse_events(events);
    input_recompute_status();
}

void input_apply_gamepad_state(uint8_t player, const uint8_t *next) {
    uint8_t *slot = input_gamepad_slot(player);
    uint8_t events = 0;

    uint16_t old_buttons = (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
    uint16_t new_buttons = (uint16_t)next[2] | ((uint16_t)next[3] << 8);
    uint16_t changed_buttons = old_buttons ^ new_buttons;

    if ((changed_buttons & new_buttons) != 0) {
        events |= GAMEPAD_EVENT_BUTTON_DOWN;
    }
    if ((changed_buttons & old_buttons) != 0) {
        events |= GAMEPAD_EVENT_BUTTON_UP;
    }
    if (((slot[0] ^ next[0]) & 0x0Fu) != 0) {
        events |= GAMEPAD_EVENT_DPAD;
    }
    if (slot[1] != next[1] || slot[4] != next[4] || slot[5] != next[5] || slot[6] != next[6] || slot[7] != next[7]) {
        events |= GAMEPAD_EVENT_STICK;
    }
    if (slot[8] != next[8] || slot[9] != next[9]) {
        events |= GAMEPAD_EVENT_TRIGGER;
    }

    bool was_connected = (slot[0] & 0x80u) != 0;
    bool connected = (next[0] & 0x80u) != 0;

    memcpy(slot, next, GAMEPAD_SLOT_SIZE);
    input_update_gamepad_device_flag(player, connected);
    if (was_connected != connected) {
        events |= GAMEPAD_EVENT_DEVICE;
    }

    input_set_gamepad_events(events);
    input_recompute_status();
}
