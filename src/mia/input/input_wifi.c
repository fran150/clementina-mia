#include "input_internal.h"

#include <stdio.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#define INPUT_MAGIC_0 'M'
#define INPUT_MAGIC_1 'I'
#define INPUT_MAGIC_2 'I'
#define INPUT_MAGIC_3 'N'
#define INPUT_VERSION 1u
#define INPUT_HEADER_SIZE 12u
#define INPUT_RX_PACKET_SIZE 384u
#define INPUT_TX_PACKET_SIZE 32u

#define INPUT_PACKET_HELLO         0x01u
#define INPUT_PACKET_WELCOME       0x02u
#define INPUT_PACKET_DISCONNECT    0x04u
#define INPUT_PACKET_TEXT          0x10u
#define INPUT_PACKET_HID_EVENT     0x11u
#define INPUT_PACKET_HID_BITMAP    0x12u
#define INPUT_PACKET_MOUSE_DELTA   0x20u
#define INPUT_PACKET_GAMEPAD_STATE 0x30u
#define INPUT_PACKET_GAMEPAD_CLEAR 0x31u
#define INPUT_PACKET_CLEAR_STATE   0x40u

#define INPUT_WELCOME_ACCEPTED 0x00u
#define INPUT_WELCOME_BUSY 0x01u
#define INPUT_WELCOME_UNSUPPORTED_VERSION 0x02u

#define INPUT_CAP_TEXT     (1u << 0)
#define INPUT_CAP_KEYBOARD (1u << 1)
#define INPUT_CAP_CONSUMER (1u << 2)
#define INPUT_CAP_MOUSE    (1u << 3)
#define INPUT_CAP_GAMEPAD  (1u << 4)
#define INPUT_CAP_ALL      (INPUT_CAP_TEXT | INPUT_CAP_KEYBOARD | INPUT_CAP_CONSUMER | INPUT_CAP_MOUSE | INPUT_CAP_GAMEPAD)

bool input_udp_ready;
input_wifi_session_t wifi_session;

static struct udp_pcb *input_pcb;
static uint32_t input_next_session_value;
static uint32_t input_tx_seq;
static uint8_t input_rx_packet[INPUT_RX_PACKET_SIZE];
static uint8_t input_tx_packet[INPUT_TX_PACKET_SIZE];

static void input_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
static void input_handle_datagram(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port);

bool input_wifi_available(void) {
    return input_udp_ready;
}

void input_wifi_reset_runtime_state(void) {
    memset(&wifi_session, 0, sizeof(wifi_session));
}

void input_wifi_init(void) {
    if (input_pcb != NULL) {
        return;
    }

    input_pcb = udp_new();
    if (input_pcb == NULL) {
        printf("MIA input UDP allocation failed\n");
        return;
    }

    err_t err = udp_bind(input_pcb, IP_ADDR_ANY, (uint16_t)MIA_INPUT_UDP_PORT);
    if (err != ERR_OK) {
        printf("MIA input UDP bind failed on port %u: %d\n", (unsigned)MIA_INPUT_UDP_PORT, err);
        udp_remove(input_pcb);
        input_pcb = NULL;
        return;
    }

    udp_recv(input_pcb, input_udp_recv, NULL);
    input_udp_ready = true;
    printf("MIA input UDP listening on port %u\n", (unsigned)MIA_INPUT_UDP_PORT);
}

static void input_apply_device_flags_from_wifi_capabilities(uint16_t capabilities) {
    uint8_t flags = 0;
    if ((capabilities & INPUT_CAP_KEYBOARD) != 0) {
        flags |= INPUT_DEVICE_KEYBOARD;
    }
    if ((capabilities & INPUT_CAP_CONSUMER) != 0) {
        flags |= INPUT_DEVICE_CONSUMER;
    }
    if ((capabilities & INPUT_CAP_MOUSE) != 0) {
        flags |= INPUT_DEVICE_MOUSE;
    }

    flags |= mem[INPUT_DEVICE_FLAGS_OFFSET] &
             (INPUT_DEVICE_GAMEPAD_0 | INPUT_DEVICE_GAMEPAD_1 | INPUT_DEVICE_GAMEPAD_2 | INPUT_DEVICE_GAMEPAD_3);
    input_set_device_flags(flags);
}

static uint32_t input_next_session_id(const ip_addr_t *addr, uint16_t port) {
    (void)addr;

    input_next_session_value++;
    if (input_next_session_value == 0) {
        input_next_session_value = 1;
    }

    uint32_t id = (input_next_session_value << 16) ^
                  ((uint32_t)port << 1) ^
                  input_tx_seq ^
                  0xA5C31E7Du;
    if (id == 0) {
        id = 1;
    }
    return id;
}

static uint32_t input_next_tx_seq(void) {
    input_tx_seq++;
    if (input_tx_seq == 0) {
        input_tx_seq = 1;
    }
    return input_tx_seq;
}

static bool input_send_welcome(uint8_t status, uint32_t session, const ip_addr_t *addr, uint16_t port) {
    if (input_pcb == NULL) {
        return false;
    }

    memset(input_tx_packet, 0, INPUT_HEADER_SIZE + 7u);
    input_tx_packet[0] = INPUT_MAGIC_0;
    input_tx_packet[1] = INPUT_MAGIC_1;
    input_tx_packet[2] = INPUT_MAGIC_2;
    input_tx_packet[3] = INPUT_MAGIC_3;
    input_tx_packet[4] = INPUT_VERSION;
    input_tx_packet[5] = INPUT_PACKET_WELCOME;
    input_write_u16(&input_tx_packet[6], (uint16_t)input_next_tx_seq());
    input_write_u32(&input_tx_packet[8], 0);

    input_tx_packet[12] = status;
    input_write_u32(&input_tx_packet[13], session);
    input_write_u16(&input_tx_packet[17], INPUT_CAP_ALL);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, INPUT_HEADER_SIZE + 7u, PBUF_RAM);
    if (p == NULL) {
        return false;
    }

    err_t err = pbuf_take(p, input_tx_packet, INPUT_HEADER_SIZE + 7u);
    if (err == ERR_OK) {
        err = udp_sendto(input_pcb, p, addr, port);
    }
    pbuf_free(p);
    return err == ERR_OK;
}

static bool input_seq_is_newer(uint16_t seq, uint16_t last_seq) {
    uint16_t delta = (uint16_t)(seq - last_seq);
    return delta != 0 && delta < 0x8000u;
}

static bool input_accepts_session_packet(uint32_t session, uint16_t seq, const ip_addr_t *addr, uint16_t port) {
    if (!wifi_session.active || session != wifi_session.session) {
        return false;
    }
    if (wifi_session.port != port || !ip_addr_cmp(&wifi_session.addr, addr)) {
        return false;
    }
    if (!input_seq_is_newer(seq, wifi_session.last_seq)) {
        return false;
    }

    wifi_session.last_seq = seq;
    return true;
}

void input_invalidate_wifi_session(bool publish_events) {
    memset(&wifi_session, 0, sizeof(wifi_session));
    input_clear_live_state(publish_events);
}

static void input_accept_wifi_session(uint16_t seq, uint16_t capabilities, const ip_addr_t *addr, uint16_t port) {
    bool replacing = wifi_session.active;

    input_clear_live_state(replacing);
    wifi_session.active = true;
    ip_addr_copy(wifi_session.addr, *addr);
    wifi_session.port = port;
    wifi_session.session = input_next_session_id(addr, port);
    wifi_session.last_seq = seq;
    wifi_session.capabilities = capabilities;
    input_apply_device_flags_from_wifi_capabilities(capabilities);
    input_recompute_status();
}

static void input_handle_hello(const uint8_t *payload, uint16_t payload_len, uint16_t seq, uint8_t version, const ip_addr_t *addr, uint16_t port) {
    if (version != INPUT_VERSION) {
        (void)input_send_welcome(INPUT_WELCOME_UNSUPPORTED_VERSION, 0, addr, port);
        return;
    }

    if (payload_len < 3u) {
        return;
    }

    uint16_t capabilities = input_read_u16(payload);
    uint8_t name_len = payload[2];
    if (payload_len != (uint16_t)(3u + name_len)) {
        return;
    }

    if (active_mode != MIA_INPUT_MODE_WIFI) {
        (void)input_send_welcome(INPUT_WELCOME_BUSY, 0, addr, port);
        return;
    }

    input_accept_wifi_session(seq, capabilities, addr, port);
    (void)input_send_welcome(INPUT_WELCOME_ACCEPTED, wifi_session.session, addr, port);
}

static void input_handle_text(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len < 1u) {
        return;
    }

    uint8_t count = payload[0];
    if (payload_len != (uint16_t)(1u + count)) {
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        input_enqueue_text(payload[1u + i]);
    }
    input_recompute_status();
}

static void input_handle_hid_event(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 6u) {
        return;
    }

    uint16_t usage_page = input_read_u16(payload);
    uint16_t usage_id = input_read_u16(&payload[2]);
    uint8_t flags = payload[4];
    uint8_t text = payload[5];
    bool down = (flags & 0x01u) != 0;

    input_set_hid_usage(usage_page, usage_id, down);

    if (usage_page == HID_PAGE_KEYBOARD && down && text != 0) {
        input_enqueue_text(text);
        input_recompute_status();
    }
}

static void input_handle_hid_bitmap(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 34u) {
        return;
    }

    input_set_hid_bitmap(input_read_u16(payload), &payload[2]);
}

static void input_handle_mouse_delta(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 5u) {
        return;
    }

    input_apply_mouse_delta(payload);
}

static void input_handle_gamepad_state(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 11u || payload[0] >= 4u) {
        return;
    }

    input_apply_gamepad_state(payload[0], &payload[1]);
}

static void input_handle_gamepad_clear(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 1u || payload[0] >= 4u) {
        return;
    }

    input_clear_gamepad_slot(payload[0], true);
    input_recompute_status();
}

static void input_handle_clear_state(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 1u) {
        return;
    }

    uint8_t mask = payload[0];
    if ((mask & 0x80u) != 0) {
        mask |= 0x1Fu;
    }

    if ((mask & 0x01u) != 0) {
        input_clear_text_fifo();
    }
    if ((mask & 0x02u) != 0) {
        uint8_t events = input_bitmap_any(input_keyboard_bitmap()) ? KEY_EVENT_KEY_UP : 0;
        memset(input_keyboard_bitmap(), 0, 32u);
        input_set_keyboard_events(events);
    }
    if ((mask & 0x04u) != 0) {
        uint8_t events = input_bitmap_any(input_consumer_bitmap()) ? KEY_EVENT_CONSUMER_UP : 0;
        memset(input_consumer_bitmap(), 0, 32u);
        input_set_keyboard_events(events);
    }
    if ((mask & 0x08u) != 0) {
        input_clear_mouse(true);
    }
    if ((mask & 0x10u) != 0) {
        input_clear_gamepads(true);
    }

    input_apply_device_flags_from_wifi_capabilities(wifi_session.capabilities);
    input_recompute_status();
}

static void input_handle_datagram(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port) {
    if (packet_len < INPUT_HEADER_SIZE ||
        packet[0] != INPUT_MAGIC_0 ||
        packet[1] != INPUT_MAGIC_1 ||
        packet[2] != INPUT_MAGIC_2 ||
        packet[3] != INPUT_MAGIC_3) {
        return;
    }

    uint8_t version = packet[4];
    uint8_t type = packet[5];
    uint16_t seq = input_read_u16(&packet[6]);
    uint32_t session = input_read_u32(&packet[8]);
    const uint8_t *payload = &packet[INPUT_HEADER_SIZE];
    uint16_t payload_len = (uint16_t)(packet_len - INPUT_HEADER_SIZE);

    if (type == INPUT_PACKET_HELLO) {
        input_handle_hello(payload, payload_len, seq, version, addr, port);
        return;
    }

    if (version != INPUT_VERSION ||
        active_mode != MIA_INPUT_MODE_WIFI ||
        !input_accepts_session_packet(session, seq, addr, port)) {
        return;
    }

    switch (type) {
        case INPUT_PACKET_DISCONNECT:
            if (payload_len == 0) {
                input_invalidate_wifi_session(true);
            }
            break;
        case INPUT_PACKET_TEXT:
            input_handle_text(payload, payload_len);
            break;
        case INPUT_PACKET_HID_EVENT:
            input_handle_hid_event(payload, payload_len);
            break;
        case INPUT_PACKET_HID_BITMAP:
            input_handle_hid_bitmap(payload, payload_len);
            break;
        case INPUT_PACKET_MOUSE_DELTA:
            input_handle_mouse_delta(payload, payload_len);
            break;
        case INPUT_PACKET_GAMEPAD_STATE:
            input_handle_gamepad_state(payload, payload_len);
            break;
        case INPUT_PACKET_GAMEPAD_CLEAR:
            input_handle_gamepad_clear(payload, payload_len);
            break;
        case INPUT_PACKET_CLEAR_STATE:
            input_handle_clear_state(payload, payload_len);
            break;
        default:
            break;
    }
}

static void input_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;

    if (p == NULL) {
        return;
    }

    if (p->tot_len <= INPUT_RX_PACKET_SIZE) {
        pbuf_copy_partial(p, input_rx_packet, p->tot_len, 0);
        input_handle_datagram(input_rx_packet, p->tot_len, addr, port);
    }

    pbuf_free(p);
}
