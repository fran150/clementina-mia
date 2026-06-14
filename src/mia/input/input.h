#ifndef _MIA_INPUT_INPUT_H_
#define _MIA_INPUT_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef MIA_INPUT_UDP_PORT
#define MIA_INPUT_UDP_PORT 6503u
#endif

#define MIA_INPUT_STATE_OFFSET 0x11000u
#define MIA_INPUT_STATE_SIZE   0x80u

#define MIA_INPUT_KEYBOARD_BITMAP_OFFSET 0x11000u
#define MIA_INPUT_CONSUMER_BITMAP_OFFSET 0x11020u
#define MIA_INPUT_MOUSE_STATE_OFFSET     0x11040u
#define MIA_INPUT_CONTROL_OFFSET         0x11045u
#define MIA_INPUT_GAMEPAD_OFFSET         0x11050u

#define MIA_INPUT_TEXT_FIFO_SIZE 64u

#define INPUT_TEXT_READY      (1u << 0)
#define INPUT_KEYBOARD_DOWN   (1u << 1)
#define INPUT_CONSUMER_DOWN   (1u << 2)
#define INPUT_MOUSE_DOWN      (1u << 3)
#define INPUT_GAMEPAD_DOWN    (1u << 4)
#define INPUT_SOURCE_CONSOLE  (1u << 5)
#define INPUT_SOURCE_WIFI     (1u << 6)
#define INPUT_SOURCE_USB_HOST (1u << 7)

#define INPUT_DEVICE_KEYBOARD  (1u << 0)
#define INPUT_DEVICE_CONSUMER  (1u << 1)
#define INPUT_DEVICE_MOUSE     (1u << 2)
#define INPUT_DEVICE_GAMEPAD_0 (1u << 3)
#define INPUT_DEVICE_GAMEPAD_1 (1u << 4)
#define INPUT_DEVICE_GAMEPAD_2 (1u << 5)
#define INPUT_DEVICE_GAMEPAD_3 (1u << 6)

#define KEY_EVENT_TEXT          (1u << 0)
#define KEY_EVENT_KEY_DOWN      (1u << 1)
#define KEY_EVENT_KEY_UP        (1u << 2)
#define KEY_EVENT_CONSUMER_DOWN (1u << 3)
#define KEY_EVENT_CONSUMER_UP   (1u << 4)
#define KEY_EVENT_DEVICE        (1u << 5)

#define MOUSE_EVENT_BUTTON_DOWN (1u << 0)
#define MOUSE_EVENT_BUTTON_UP   (1u << 1)
#define MOUSE_EVENT_MOVE        (1u << 2)
#define MOUSE_EVENT_SCROLL      (1u << 3)
#define MOUSE_EVENT_DEVICE      (1u << 4)

#define GAMEPAD_EVENT_BUTTON_DOWN (1u << 0)
#define GAMEPAD_EVENT_BUTTON_UP   (1u << 1)
#define GAMEPAD_EVENT_DPAD        (1u << 2)
#define GAMEPAD_EVENT_STICK       (1u << 3)
#define GAMEPAD_EVENT_TRIGGER     (1u << 4)
#define GAMEPAD_EVENT_DEVICE      (1u << 5)

typedef enum {
    MIA_INPUT_MODE_CONSOLE = 0,
    MIA_INPUT_MODE_WIFI = 1,
    MIA_INPUT_MODE_USB_HOST = 2,
} mia_input_mode_t;

void mia_input_init(void);
void mia_input_reset_runtime_state(void);
void mia_input_service(void);
void mia_input_report_errors(void);

bool mia_input_set_mode(mia_input_mode_t mode);
mia_input_mode_t mia_input_get_mode(void);
bool mia_input_set_probe(uint8_t probe_id, uint8_t byte_offset);

void mia_input_console_byte(uint8_t value);
void mia_input_console_end_capture(void);

void mia_input_core1_on_char_read(void);
void mia_input_core1_refresh_registers(void);

const char *mia_input_mode_name(mia_input_mode_t mode);
void mia_input_print_status(void);

#endif
