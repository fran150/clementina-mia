#ifndef _MIA_INPUT_INTERNAL_H_
#define _MIA_INPUT_INTERNAL_H_

#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#include "lwip/ip_addr.h"

#include "mem/mem.h"

#ifndef MIA_USB_MODE_DEVICE
#define MIA_USB_MODE_DEVICE 0
#endif

#ifndef MIA_USB_MODE_HOST
#define MIA_USB_MODE_HOST 1
#endif

#ifndef MIA_USB_MODE
#define MIA_USB_MODE MIA_USB_MODE_DEVICE
#endif

#ifndef MIA_INPUT_DEFAULT_MODE
#define MIA_INPUT_DEFAULT_MODE MIA_INPUT_MODE_CONSOLE
#endif

#define HID_PAGE_KEYBOARD 0x0007u
#define HID_PAGE_CONSUMER 0x000Cu

#define MOUSE_BUTTON_MASK 0x1Fu
#define GAMEPAD_SLOT_SIZE 10u

#define INPUT_CAP_TEXT     (1u << 0)
#define INPUT_CAP_KEYBOARD (1u << 1)
#define INPUT_CAP_CONSUMER (1u << 2)
#define INPUT_CAP_MOUSE    (1u << 3)
#define INPUT_CAP_GAMEPAD  (1u << 4)
#define INPUT_CAP_ALL      (INPUT_CAP_TEXT | INPUT_CAP_KEYBOARD | INPUT_CAP_CONSUMER | INPUT_CAP_MOUSE | INPUT_CAP_GAMEPAD)

enum {
    INPUT_DEVICE_FLAGS_OFFSET = MIA_INPUT_CONTROL_OFFSET,
    KEYBOARD_EVENT_FLAGS_OFFSET,
    KEYBOARD_EVENT_MASK_OFFSET,
    KEYBOARD_EVENT_ACK_OFFSET,
    MOUSE_EVENT_FLAGS_OFFSET,
    MOUSE_EVENT_MASK_OFFSET,
    MOUSE_EVENT_ACK_OFFSET,
    GAMEPAD_EVENT_FLAGS_OFFSET,
    GAMEPAD_EVENT_MASK_OFFSET,
    GAMEPAD_EVENT_ACK_OFFSET,
};

typedef struct {
    bool active;
    ip_addr_t addr;
    uint16_t port;
    uint32_t session;
    uint16_t last_seq;
    uint16_t capabilities;
} input_wifi_session_t;

extern volatile uint8_t cached_input_status;
extern volatile uint8_t cached_input_char;
extern volatile uint8_t cached_input_char_count;

extern mia_input_mode_t active_mode;
extern bool input_udp_ready;
extern input_wifi_session_t wifi_session;

static inline uint16_t input_read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t input_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void input_write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void input_write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static inline uint8_t *input_keyboard_bitmap(void) {
    return &mem[MIA_INPUT_KEYBOARD_BITMAP_OFFSET];
}

static inline uint8_t *input_consumer_bitmap(void) {
    return &mem[MIA_INPUT_CONSUMER_BITMAP_OFFSET];
}

static inline uint8_t *input_mouse_state(void) {
    return &mem[MIA_INPUT_MOUSE_STATE_OFFSET];
}

static inline uint8_t *input_gamepad_slot(uint8_t player) {
    return &mem[MIA_INPUT_GAMEPAD_OFFSET + (uint32_t)player * GAMEPAD_SLOT_SIZE];
}

void input_publish_registers(void);
void input_recompute_status(void);

void input_publish_text_snapshot(void);
void input_enqueue_text(uint8_t value);
void input_clear_text_fifo(void);

bool input_bitmap_any(const uint8_t *bitmap);
bool input_gamepad_any_digital_down(void);
void input_set_keyboard_events(uint8_t flags);
void input_set_mouse_events(uint8_t flags);
void input_set_gamepad_events(uint8_t flags);
void input_set_device_flags(uint8_t flags);
void input_update_irqs(void);
void input_apply_event_acks(void);
void input_clear_keyboard_consumer(bool publish_events);
void input_clear_mouse(bool publish_events);
void input_clear_gamepad_slot(uint8_t player, bool publish_events);
void input_clear_gamepads(bool publish_events);
void input_clear_live_state(bool publish_events);
void input_set_hid_usage(uint16_t usage_page, uint16_t usage_id, bool down);
void input_set_hid_bitmap(uint16_t usage_page, const uint8_t *new_bitmap);
void input_apply_mouse_delta(const uint8_t *payload);
void input_apply_gamepad_state(uint8_t player, const uint8_t *next);

bool input_wifi_available(void);
void input_wifi_init(void);
void input_wifi_reset_runtime_state(void);
void input_wifi_report_errors(void);
void input_invalidate_wifi_session(bool publish_events);

#endif
