#include "input_internal.h"

#include <string.h>

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
