#include "input_internal.h"

#include <stdio.h>
#include <string.h>

#include "mem/indexes.h"
#include "mem/regs.h"

volatile uint8_t cached_input_status;
volatile uint8_t cached_input_char;
volatile uint8_t cached_input_char_count;

mia_input_mode_t active_mode;

void input_publish_registers(void) {
    mia_regs->input_status = cached_input_status;
    mia_regs->input_char = cached_input_char;
    mia_regs->input_char_count = cached_input_char_count;
}

void input_recompute_status(void) {
    uint8_t status = 0;

    switch (active_mode) {
        case MIA_INPUT_MODE_CONSOLE:
            status |= INPUT_SOURCE_CONSOLE;
            break;
        case MIA_INPUT_MODE_WIFI:
            status |= INPUT_SOURCE_WIFI;
            break;
        case MIA_INPUT_MODE_USB_HOST:
            status |= INPUT_SOURCE_USB_HOST;
            break;
    }

    if (input_bitmap_any(input_keyboard_bitmap())) {
        status |= INPUT_KEYBOARD_DOWN;
    }
    if (input_bitmap_any(input_consumer_bitmap())) {
        status |= INPUT_CONSUMER_DOWN;
    }
    if ((input_mouse_state()[0] & MOUSE_BUTTON_MASK) != 0) {
        status |= INPUT_MOUSE_DOWN;
    }
    if (input_gamepad_any_digital_down()) {
        status |= INPUT_GAMEPAD_DOWN;
    }

    input_publish_text_snapshot();
    if (cached_input_char_count != 0) {
        status |= INPUT_TEXT_READY;
    }
    cached_input_status = status;
    input_publish_text_snapshot();

    input_publish_registers();
}

void mia_input_core1_refresh_registers(void) {
    input_publish_registers();
}

static void input_configure_index(uint8_t index_id, uint32_t start, uint32_t length, bool step) {
    idx[index_id].current_addr = start;
    idx[index_id].default_addr = start;
    idx[index_id].limit_addr = start + length;
    idx[index_id].step = 1u;
    idx[index_id].flags = (1u << IDX_FLAG_WRAP_ENA);
    if (step) {
        idx[index_id].flags |= (1u << IDX_FLAG_R_STP_ENA) | (1u << IDX_FLAG_W_STP_ENA);
    }
    idx[index_id].reserved = 0;
}

static void input_configure_indexes(void) {
    for (uint8_t i = 0; i < 8u; i++) {
        input_configure_index((uint8_t)(0x50u + i), MIA_INPUT_KEYBOARD_BITMAP_OFFSET, 1u, false);
        input_configure_index((uint8_t)(0x58u + i), MIA_INPUT_CONSUMER_BITMAP_OFFSET, 1u, false);
    }

    input_configure_index(0x60, MIA_INPUT_STATE_OFFSET, MIA_INPUT_STATE_SIZE, true);
    input_configure_index(0x61, MIA_INPUT_KEYBOARD_BITMAP_OFFSET, 32u, true);
    input_configure_index(0x62, MIA_INPUT_CONSUMER_BITMAP_OFFSET, 32u, true);
    input_configure_index(0x63, MIA_INPUT_MOUSE_STATE_OFFSET, 5u, true);
    input_configure_index(0x64, MIA_INPUT_GAMEPAD_OFFSET, GAMEPAD_SLOT_SIZE, true);
    input_configure_index(0x65, MIA_INPUT_GAMEPAD_OFFSET + GAMEPAD_SLOT_SIZE, GAMEPAD_SLOT_SIZE, true);
    input_configure_index(0x66, MIA_INPUT_GAMEPAD_OFFSET + GAMEPAD_SLOT_SIZE * 2u, GAMEPAD_SLOT_SIZE, true);
    input_configure_index(0x67, MIA_INPUT_GAMEPAD_OFFSET + GAMEPAD_SLOT_SIZE * 3u, GAMEPAD_SLOT_SIZE, true);
    input_configure_index(0x68, MIA_INPUT_CONTROL_OFFSET, 11u, true);
}

static bool input_mode_available(mia_input_mode_t mode) {
    switch (mode) {
        case MIA_INPUT_MODE_CONSOLE:
#if MIA_USB_MODE == MIA_USB_MODE_HOST
            return false;
#else
            return true;
#endif
        case MIA_INPUT_MODE_WIFI:
            return input_wifi_available();
        case MIA_INPUT_MODE_USB_HOST:
#if MIA_USB_MODE == MIA_USB_MODE_HOST
            return true;
#else
            return false;
#endif
        default:
            return false;
    }
}

void mia_input_reset_runtime_state(void) {
    input_clear_text_fifo();
    memset(&mem[MIA_INPUT_STATE_OFFSET], 0, MIA_INPUT_STATE_SIZE);
    input_wifi_reset_runtime_state();
    input_configure_indexes();

#if MIA_USB_MODE == MIA_USB_MODE_HOST
    active_mode = MIA_INPUT_MODE_USB_HOST;
#else
    active_mode = (mia_input_mode_t)MIA_INPUT_DEFAULT_MODE;
    if (!input_mode_available(active_mode)) {
        active_mode = MIA_INPUT_MODE_CONSOLE;
    }
#endif

    input_recompute_status();
}

void mia_input_init(void) {
    input_wifi_init();
    mia_input_reset_runtime_state();
}

void mia_input_report_errors(void) {
    input_wifi_report_errors();
}

bool mia_input_set_mode(mia_input_mode_t mode) {
    if (!input_mode_available(mode)) {
        return false;
    }

    if (mode == active_mode) {
        return true;
    }

    if (active_mode == MIA_INPUT_MODE_WIFI) {
        input_invalidate_wifi_session(true);
    } else {
        input_clear_live_state(true);
    }

    active_mode = mode;

    if (mode == MIA_INPUT_MODE_WIFI) {
        input_invalidate_wifi_session(false);
    } else {
        input_clear_live_state(false);
    }

    input_recompute_status();
    return true;
}

mia_input_mode_t mia_input_get_mode(void) {
    return active_mode;
}

bool mia_input_set_probe(uint8_t probe_id, uint8_t byte_offset) {
    uint8_t offset = byte_offset & 0x1Fu;

    if (probe_id < 8u) {
        input_configure_index((uint8_t)(0x50u + probe_id), MIA_INPUT_KEYBOARD_BITMAP_OFFSET + offset, 1u, false);
        return true;
    }

    if (probe_id < 16u) {
        input_configure_index((uint8_t)(0x58u + probe_id - 8u), MIA_INPUT_CONSUMER_BITMAP_OFFSET + offset, 1u, false);
        return true;
    }

    return false;
}

const char *mia_input_mode_name(mia_input_mode_t mode) {
    switch (mode) {
        case MIA_INPUT_MODE_CONSOLE:
            return "console";
        case MIA_INPUT_MODE_WIFI:
            return "wifi";
        case MIA_INPUT_MODE_USB_HOST:
            return "usb_host";
        default:
            return "unknown";
    }
}

static void input_print_flag(bool *any, const char *name) {
    printf(*any ? "," : " (");
    printf("%s", name);
    *any = true;
}

static void input_print_status_bits(uint8_t status) {
    bool any = false;

    printf("0x%02X", (unsigned)status);
    if (status & INPUT_TEXT_READY)      input_print_flag(&any, "TEXT");
    if (status & INPUT_KEYBOARD_DOWN)   input_print_flag(&any, "KEYBOARD");
    if (status & INPUT_CONSUMER_DOWN)   input_print_flag(&any, "CONSUMER");
    if (status & INPUT_MOUSE_DOWN)      input_print_flag(&any, "MOUSE");
    if (status & INPUT_GAMEPAD_DOWN)    input_print_flag(&any, "GAMEPAD");
    if (status & INPUT_SOURCE_CONSOLE)  input_print_flag(&any, "CONSOLE");
    if (status & INPUT_SOURCE_WIFI)     input_print_flag(&any, "WIFI");
    if (status & INPUT_SOURCE_USB_HOST) input_print_flag(&any, "USB_HOST");
    if (any) printf(")");
}

static void input_print_device_flags(uint8_t flags) {
    bool any = false;

    printf("0x%02X", (unsigned)flags);
    if (flags & INPUT_DEVICE_KEYBOARD)  input_print_flag(&any, "KEYBOARD");
    if (flags & INPUT_DEVICE_CONSUMER)  input_print_flag(&any, "CONSUMER");
    if (flags & INPUT_DEVICE_MOUSE)     input_print_flag(&any, "MOUSE");
    if (flags & INPUT_DEVICE_GAMEPAD_0) input_print_flag(&any, "PAD0");
    if (flags & INPUT_DEVICE_GAMEPAD_1) input_print_flag(&any, "PAD1");
    if (flags & INPUT_DEVICE_GAMEPAD_2) input_print_flag(&any, "PAD2");
    if (flags & INPUT_DEVICE_GAMEPAD_3) input_print_flag(&any, "PAD3");
    if (any) printf(")");
}

static void input_print_capabilities(uint16_t capabilities) {
    bool any = false;

    printf("0x%04X", (unsigned)capabilities);
    if (capabilities & INPUT_CAP_TEXT)     input_print_flag(&any, "TEXT");
    if (capabilities & INPUT_CAP_KEYBOARD) input_print_flag(&any, "KEYBOARD");
    if (capabilities & INPUT_CAP_CONSUMER) input_print_flag(&any, "CONSUMER");
    if (capabilities & INPUT_CAP_MOUSE)    input_print_flag(&any, "MOUSE");
    if (capabilities & INPUT_CAP_GAMEPAD)  input_print_flag(&any, "GAMEPAD");
    if (any) printf(")");
}

void mia_input_print_status(void) {
    printf("Input: %s", mia_input_mode_name(active_mode));
    if (active_mode == MIA_INPUT_MODE_WIFI) {
        printf("  UDP:%s", input_udp_ready ? "ready" : "unavailable");
        printf("  client:%s", wifi_session.active ? "active" : "none");
    }
    printf("  status:0x%02X  chars:%u\n", cached_input_status, cached_input_char_count);
}

void mia_input_print_detail(void) {
    printf("Input:\n");
    printf("  mode:    %s\n", mia_input_mode_name(active_mode));
    printf("  status:  ");
    input_print_status_bits(cached_input_status);
    printf("  chars:%u  current:0x%02X\n",
           (unsigned)cached_input_char_count,
           (unsigned)cached_input_char);

    printf("  UDP:     %s  port:%u\n",
           input_udp_ready ? "ready" : "unavailable",
           (unsigned)MIA_INPUT_UDP_PORT);

    if (wifi_session.active) {
        printf("  client:  %s:%u  session:0x%08lX  last-seq:%u\n",
               ipaddr_ntoa(&wifi_session.addr),
               (unsigned)wifi_session.port,
               (unsigned long)wifi_session.session,
               (unsigned)wifi_session.last_seq);
        printf("  caps:    ");
        input_print_capabilities(wifi_session.capabilities);
        printf("\n");
    } else {
        printf("  client:  none\n");
    }

    printf("  devices: ");
    input_print_device_flags(mem[INPUT_DEVICE_FLAGS_OFFSET]);
    printf("\n");

    printf("  keyboard events: flags:0x%02X  mask:0x%02X  ack:0x%02X\n",
           (unsigned)mem[KEYBOARD_EVENT_FLAGS_OFFSET],
           (unsigned)mem[KEYBOARD_EVENT_MASK_OFFSET],
           (unsigned)mem[KEYBOARD_EVENT_ACK_OFFSET]);
    printf("  mouse events:    flags:0x%02X  mask:0x%02X  ack:0x%02X\n",
           (unsigned)mem[MOUSE_EVENT_FLAGS_OFFSET],
           (unsigned)mem[MOUSE_EVENT_MASK_OFFSET],
           (unsigned)mem[MOUSE_EVENT_ACK_OFFSET]);
    printf("  gamepad events:  flags:0x%02X  mask:0x%02X  ack:0x%02X\n",
           (unsigned)mem[GAMEPAD_EVENT_FLAGS_OFFSET],
           (unsigned)mem[GAMEPAD_EVENT_MASK_OFFSET],
           (unsigned)mem[GAMEPAD_EVENT_ACK_OFFSET]);

    uint8_t *mouse = input_mouse_state();
    printf("  mouse:   buttons:0x%02X  dx:0x%02X  dy:0x%02X  wheel-x:0x%02X  wheel-y:0x%02X\n",
           (unsigned)(mouse[0] & MOUSE_BUTTON_MASK),
           (unsigned)mouse[1],
           (unsigned)mouse[2],
           (unsigned)mouse[3],
           (unsigned)mouse[4]);

    for (uint8_t i = 0; i < 4u; i++) {
        uint8_t *slot = input_gamepad_slot(i);
        uint16_t buttons = (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
        printf("  pad%u:    %s  dpad:0x%X  buttons:0x%04X  lx:%u  ly:%u  rx:%u  ry:%u  lt:%u  rt:%u\n",
               (unsigned)i,
               (slot[0] & 0x80u) ? "connected" : "none",
               (unsigned)(slot[0] & 0x0Fu),
               (unsigned)buttons,
               (unsigned)slot[4],
               (unsigned)slot[5],
               (unsigned)slot[6],
               (unsigned)slot[7],
               (unsigned)slot[8],
               (unsigned)slot[9]);
    }
}

void mia_input_console_byte(uint8_t value) {
    if (active_mode != MIA_INPUT_MODE_CONSOLE) {
        return;
    }

    input_enqueue_text(value);
    input_recompute_status();
}

void mia_input_console_end_capture(void) {
    input_recompute_status();
}

void mia_input_service(void) {
    input_apply_event_acks();
    input_update_irqs();
}
