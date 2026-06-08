#include "video.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "etc/status.h"
#include "irq/irq.h"
#include "mem/indexes.h"
#include "mem/mem.h"
#include "video_packets.h"

#ifndef MIA_WIFI_PASSWORD
#define MIA_WIFI_PASSWORD ""
#endif

#define MIA_VIDEO_MAX_CHUNKS 154u
#define MIA_VIDEO_SEND_BUDGET 4u

typedef struct {
    bool active;
    ip_addr_t addr;
    uint16_t port;
    uint32_t session_id;
    uint32_t client_frame_id;
} mia_video_session_t;

typedef struct {
    bool valid;
    uint8_t map_index;
    uint16_t request_id;
    uint32_t frame_id;
    uint32_t last_complete_frame_id;
    uint16_t page_count;
    uint16_t chunk_count;
    uint16_t next_chunk_to_send;
    bool initial_send_done;
} mia_video_response_t;

uint8_t mia_video_dirty_maps[2][MIA_VIDEO_DIRTY_MAP_SIZE] __attribute__((aligned(4)));
volatile uint8_t mia_video_active_dirty_index;

volatile bool mia_video_rotate_request;
volatile bool mia_video_rotate_done;
volatile uint8_t mia_video_rotated_pending_index;

volatile bool mia_video_mark_all_request;
volatile bool mia_video_mark_all_done;

static struct udp_pcb *video_pcb;
static bool video_udp_ready;
static mia_video_session_t video_session;
static mia_video_response_t video_response;
static uint16_t video_pending_pages[MIA_VIDEO_SYNC_PAGE_COUNT];
static uint16_t video_repair_chunks[MIA_VIDEO_MAX_CHUNKS];
static uint16_t video_repair_count;
static uint16_t video_repair_pos;
static uint32_t video_frame_id;
static uint32_t video_next_seq_value;
static uint32_t video_last_peer_seq;
static uint8_t video_rx_packet[MIA_VIDEO_UDP_PAYLOAD_SIZE];
static uint8_t video_tx_packet[MIA_VIDEO_UDP_PAYLOAD_SIZE];

static void video_clear_dirty_map(uint8_t map_index);
static void video_clear_dirty_maps(void);
static void video_configure_indexes(void);
static void video_configure_index(uint8_t index_id, uint32_t start, uint32_t length);
static bool video_has_dirty_pages(uint8_t map_index);
static uint16_t video_scan_dirty_pages(uint8_t map_index);
static void video_request_dirty_rotation(void);
static void video_request_mark_all_dirty(void);
static uint32_t video_next_frame_id(void);
static uint32_t video_next_seq(void);
static void video_set_frame_id(uint32_t frame_id);
static void video_set_last_response_dirty_pages(uint16_t count);
static bool video_send_packet(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port);
static uint16_t video_build_header(uint8_t packet_type, uint32_t session_id, uint32_t frame_id, uint16_t request_id, uint16_t chunk_index, uint16_t chunk_count, uint16_t payload_len);
static bool video_send_status(uint16_t status_code, uint16_t request_id, uint32_t frame_id, const ip_addr_t *addr, uint16_t port);
static bool video_send_welcome(const ip_addr_t *addr, uint16_t port);
static bool video_send_frame_chunk(uint16_t chunk_index);
static bool video_validate_hello(const mia_video_header_t *header);
static bool video_accepts_session_packet(const mia_video_header_t *header, const ip_addr_t *addr, uint16_t port);
static void video_reset_session(const ip_addr_t *addr, uint16_t port);
static void video_invalidate_session(void);
static void video_release_pending_response(bool acknowledged);
static void video_protocol_error(const ip_addr_t *addr, uint16_t port, uint16_t request_id, uint32_t frame_id);
static void video_handle_datagram(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port);
static void video_handle_request_frame(const mia_video_header_t *header, const uint8_t *payload, const ip_addr_t *addr, uint16_t port);
static void video_handle_request_frame_no_pending(uint16_t request_id, uint32_t last_complete, const ip_addr_t *addr, uint16_t port);
static void video_handle_ack_response(const mia_video_header_t *header, const ip_addr_t *addr, uint16_t port);
static void video_handle_nack_chunks(const mia_video_header_t *header, const uint8_t *payload, const ip_addr_t *addr, uint16_t port);
static void video_handle_client_status(const mia_video_header_t *header, const uint8_t *payload);
static void video_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

bool mia_video_wifi_init(void) {
#ifdef MIA_WIFI_SSID
    if (MIA_WIFI_SSID[0] == '\0') {
        printf("MIA video Wi-Fi SSID is empty; UDP video will wait for a network.\n");
        return false;
    }

    cyw43_arch_enable_sta_mode();

    uint32_t auth = strlen(MIA_WIFI_PASSWORD) == 0 ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    printf("Connecting Wi-Fi for MIA video: %s\n", MIA_WIFI_SSID);
    int rc = cyw43_arch_wifi_connect_timeout_ms(MIA_WIFI_SSID, MIA_WIFI_PASSWORD, auth, 30000);
    if (rc != 0) {
        printf("MIA video Wi-Fi connection failed: %d\n", rc);
        return false;
    }

    if (netif_default != NULL) {
        printf("MIA video Wi-Fi connected at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    } else {
        printf("MIA video Wi-Fi connected\n");
    }

    return true;
#else
    printf("MIA video Wi-Fi credentials not configured; set MIA_WIFI_SSID and MIA_WIFI_PASSWORD at build time.\n");
    return false;
#endif
}

void mia_video_init(void) {
    mia_video_reset_runtime_state();

    if (video_pcb != NULL) {
        return;
    }

    video_pcb = udp_new();
    if (video_pcb == NULL) {
        printf("MIA video UDP allocation failed\n");
        return;
    }

    err_t err = udp_bind(video_pcb, IP_ADDR_ANY, (uint16_t)MIA_VIDEO_UDP_PORT);
    if (err != ERR_OK) {
        printf("MIA video UDP bind failed on port %u: %d\n", (unsigned)MIA_VIDEO_UDP_PORT, err);
        udp_remove(video_pcb);
        video_pcb = NULL;
        return;
    }

    udp_recv(video_pcb, video_udp_recv, NULL);
    video_udp_ready = true;
    printf("MIA video UDP listening on port %u\n", (unsigned)MIA_VIDEO_UDP_PORT);
}

void mia_video_reset_runtime_state(void) {
    struct udp_pcb *saved_pcb = video_pcb;
    bool saved_udp_ready = video_udp_ready;
    uint32_t saved_next_seq = video_next_seq_value;

    memset(&video_session, 0, sizeof(video_session));
    memset(&video_response, 0, sizeof(video_response));
    memset(video_pending_pages, 0, sizeof(video_pending_pages));
    video_repair_count = 0;
    video_repair_pos = 0;
    video_frame_id = 0;
    video_last_peer_seq = 0;
    video_clear_dirty_maps();
    mia_video_active_dirty_index = 0;
    mia_video_rotate_request = false;
    mia_video_rotate_done = false;
    mia_video_rotated_pending_index = 0;
    mia_video_mark_all_request = false;
    mia_video_mark_all_done = false;

    if (mem != NULL) {
        mem[MIA_VIDEO_LOCAL_VERSION_OFFSET] = MIA_VIDEO_LAYOUT_VERSION;
        video_set_frame_id(0);
        video_set_last_response_dirty_pages(0);
    }

    mia_status_clear_flag(MIA_STAT_VIDEO_FRAME_REQUESTED | MIA_STAT_VIDEO_FRAME_SENT);

    video_pcb = saved_pcb;
    video_udp_ready = saved_udp_ready;
    video_next_seq_value = saved_next_seq;
}

void mia_video_service(void) {
    if (!video_udp_ready || !video_session.active || !video_response.valid) {
        return;
    }

    uint32_t budget = MIA_VIDEO_SEND_BUDGET;

    while (budget > 0 && video_repair_pos < video_repair_count) {
        if (!video_send_frame_chunk(video_repair_chunks[video_repair_pos])) {
            return;
        }
        video_repair_pos++;
        budget--;
    }

    if (video_repair_pos >= video_repair_count) {
        video_repair_count = 0;
        video_repair_pos = 0;
    }

    while (budget > 0 && video_response.next_chunk_to_send < video_response.chunk_count) {
        if (!video_send_frame_chunk(video_response.next_chunk_to_send)) {
            return;
        }
        video_response.next_chunk_to_send++;
        budget--;
    }

    if (!video_response.initial_send_done &&
        video_response.next_chunk_to_send >= video_response.chunk_count) {
        video_response.initial_send_done = true;
        video_set_last_response_dirty_pages(video_response.page_count);
        mia_status_set_flag(MIA_STAT_VIDEO_FRAME_SENT);
        mia_irq_set_flag(IRQ_VIDEO_FRAME_SENT);
    }
}

void mia_video_enable(void) {
    memset(mem, 0, MIA_VIDEO_STATE_SIZE);
    mem[MIA_VIDEO_LOCAL_VERSION_OFFSET] = MIA_VIDEO_LAYOUT_VERSION;
    video_set_frame_id(0);
    video_set_last_response_dirty_pages(0);

    video_frame_id = 0;
    video_session.client_frame_id = 0;
    video_release_pending_response(false);
    video_clear_dirty_maps();
    mia_video_active_dirty_index = 0;
    video_configure_indexes();
    mia_status_clear_flag(MIA_STAT_VIDEO_FRAME_REQUESTED | MIA_STAT_VIDEO_FRAME_SENT);
    mia_video_force_full_refresh();
}

void mia_video_force_full_refresh(void) {
    video_request_mark_all_dirty();
}

void mia_video_set_mode(uint8_t mode) {
    mem[MIA_VIDEO_MODE_OFFSET] = mode;
    mia_video_mark_dirty(MIA_VIDEO_MODE_OFFSET);
}

static void video_clear_dirty_map(uint8_t map_index) {
    memset(mia_video_dirty_maps[map_index], 0, MIA_VIDEO_DIRTY_MAP_SIZE);
}

static void video_clear_dirty_maps(void) {
    video_clear_dirty_map(0);
    video_clear_dirty_map(1);
}

static void video_configure_indexes(void) {
    video_configure_index(0x70, 0x00000u, 32u);
    video_configure_index(0x71, 0x00004u, 4u);
    video_configure_index(0x72, 0x00008u, 2u);

    video_configure_index(0x80, MIA_VIDEO_RENDER_CONTROL_OFFSET, 32u);
    video_configure_index(0x81, 0x00021u, 1u);
    video_configure_index(0x82, 0x00022u, 2u);
    video_configure_index(0x83, 0x00024u, 2u);
    video_configure_index(0x84, 0x00026u, 2u);
    video_configure_index(0x85, 0x00028u, 5u);
    video_configure_index(0x86, 0x0002Du, 2u);
    video_configure_index(0x87, 0x0002Fu, 1u);
    video_configure_index(0x88, 0x00030u, 1u);

    for (uint32_t i = 0; i < 16u; i++) {
        video_configure_index((uint8_t)(0x90u + i), MIA_VIDEO_PALETTE_OFFSET + i * 16u, 16u);
    }

    for (uint32_t i = 0; i < 8u; i++) {
        video_configure_index((uint8_t)(0xA0u + i), MIA_VIDEO_CHR_OFFSET + i * 6144u, 6144u);
        video_configure_index((uint8_t)(0xA8u + i), MIA_VIDEO_BG_NT_OFFSET + i * 1000u, 1000u);
        video_configure_index((uint8_t)(0xB0u + i), MIA_VIDEO_BG_ATTR_OFFSET + i * 1000u, 1000u);
    }

    video_configure_index(0xB8, MIA_VIDEO_OVERLAY_NT_OFFSET, 1000u);
    video_configure_index(0xB9, MIA_VIDEO_OVERLAY_ATTR_OFFSET, 1000u);

    for (uint32_t i = 0; i < 32u; i++) {
        video_configure_index((uint8_t)(0xC0u + i), MIA_VIDEO_OAM_OFFSET + i * 5u, 5u);
    }
}

static void video_configure_index(uint8_t index_id, uint32_t start, uint32_t length) {
    idx[index_id].current_addr = start;
    idx[index_id].default_addr = start;
    idx[index_id].limit_addr = start + length;
    idx[index_id].step = 1u;
    idx[index_id].flags = (1u << IDX_FLAG_R_STP_ENA) |
                          (1u << IDX_FLAG_W_STP_ENA) |
                          (1u << IDX_FLAG_WRAP_ENA);
    idx[index_id].reserved = 0;
}

static bool video_has_dirty_pages(uint8_t map_index) {
    const uint8_t *dirty = mia_video_dirty_maps[map_index];
    for (uint32_t byte_i = 0; byte_i < MIA_VIDEO_DIRTY_MAP_SIZE; byte_i++) {
        uint8_t bits = dirty[byte_i];
        if (byte_i == 0) {
            bits &= 0xFEu;
        } else if (byte_i == MIA_VIDEO_DIRTY_MAP_SIZE - 1u) {
            bits &= 0x07u;
        }

        if (bits != 0) {
            return true;
        }
    }

    return false;
}

static uint16_t video_scan_dirty_pages(uint8_t map_index) {
    const uint8_t *dirty = mia_video_dirty_maps[map_index];
    uint16_t count = 0;

    for (uint32_t byte_i = 0; byte_i < MIA_VIDEO_DIRTY_MAP_SIZE; byte_i++) {
        uint8_t bits = dirty[byte_i];
        if (byte_i == 0) {
            bits &= 0xFEu;
        } else if (byte_i == MIA_VIDEO_DIRTY_MAP_SIZE - 1u) {
            bits &= 0x07u;
        }

        while (bits != 0) {
            uint32_t bit = (uint32_t)__builtin_ctz((unsigned)bits);
            uint32_t page = byte_i * 8u + bit;
            if (page >= MIA_VIDEO_FIRST_SYNC_PAGE && page < MIA_VIDEO_PAGE_COUNT) {
                video_pending_pages[count++] = (uint16_t)page;
            }
            bits &= (uint8_t)(bits - 1u);
        }
    }

    return count;
}

static void video_request_dirty_rotation(void) {
    mia_video_rotate_done = false;
    mia_video_rotate_request = true;
    __sev();

    while (!mia_video_rotate_done) {
        tight_loop_contents();
    }
}

static void video_request_mark_all_dirty(void) {
    mia_video_mark_all_done = false;
    mia_video_mark_all_request = true;
    __sev();

    while (!mia_video_mark_all_done) {
        tight_loop_contents();
    }
}

static uint32_t video_next_frame_id(void) {
    video_frame_id++;
    if (video_frame_id == 0) {
        video_frame_id = 1;
    }

    return video_frame_id;
}

static uint32_t video_next_seq(void) {
    video_next_seq_value++;
    if (video_next_seq_value == 0) {
        video_next_seq_value = 1;
    }

    return video_next_seq_value;
}

static void video_set_frame_id(uint32_t frame_id) {
    mia_video_write_u32(&mem[MIA_VIDEO_LOCAL_FRAME_ID_OFFSET], frame_id);
}

static void video_set_last_response_dirty_pages(uint16_t count) {
    mia_video_write_u16(&mem[MIA_VIDEO_LOCAL_DIRTY_PAGES_OFFSET], count);
}

static bool video_send_packet(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port) {
    if (video_pcb == NULL) {
        return false;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, packet_len, PBUF_RAM);
    if (p == NULL) {
        return false;
    }

    err_t err = pbuf_take(p, packet, packet_len);
    if (err == ERR_OK) {
        err = udp_sendto(video_pcb, p, addr, port);
    }

    pbuf_free(p);
    return err == ERR_OK;
}

static uint16_t video_build_header(uint8_t packet_type, uint32_t session_id, uint32_t frame_id, uint16_t request_id, uint16_t chunk_index, uint16_t chunk_count, uint16_t payload_len) {
    memset(video_tx_packet, 0, MIA_VIDEO_HEADER_SIZE);
    mia_video_write_u16(&video_tx_packet[0], MIA_VIDEO_MAGIC);
    video_tx_packet[2] = MIA_VIDEO_VERSION;
    video_tx_packet[3] = packet_type;
    mia_video_write_u32(&video_tx_packet[4], session_id);
    mia_video_write_u32(&video_tx_packet[8], video_next_seq());
    mia_video_write_u32(&video_tx_packet[12], video_last_peer_seq);
    mia_video_write_u32(&video_tx_packet[16], frame_id);
    mia_video_write_u16(&video_tx_packet[20], request_id);
    mia_video_write_u16(&video_tx_packet[22], chunk_index);
    mia_video_write_u16(&video_tx_packet[24], chunk_count);
    mia_video_write_u16(&video_tx_packet[26], payload_len);

    return (uint16_t)(MIA_VIDEO_HEADER_SIZE + payload_len);
}

static bool video_send_status(uint16_t status_code, uint16_t request_id, uint32_t frame_id, const ip_addr_t *addr, uint16_t port) {
    uint16_t packet_len = video_build_header(
        MIA_VIDEO_PACKET_STATUS,
        video_session.session_id,
        frame_id,
        request_id,
        0,
        0,
        2);
    mia_video_write_u16(&video_tx_packet[MIA_VIDEO_HEADER_SIZE], status_code);

    return video_send_packet(video_tx_packet, packet_len, addr, port);
}

static bool video_send_welcome(const ip_addr_t *addr, uint16_t port) {
    uint16_t packet_len = video_build_header(
        MIA_VIDEO_PACKET_WELCOME,
        video_session.session_id,
        0,
        0,
        0,
        0,
        0);

    return video_send_packet(video_tx_packet, packet_len, addr, port);
}

static bool video_send_frame_chunk(uint16_t chunk_index) {
    if (!video_response.valid ||
        !video_session.active ||
        chunk_index >= video_response.chunk_count) {
        return false;
    }

    uint32_t first_record = (uint32_t)chunk_index * MIA_VIDEO_RECORDS_PER_CHUNK;
    uint32_t remaining = video_response.page_count - first_record;
    uint32_t record_count = remaining < MIA_VIDEO_RECORDS_PER_CHUNK ? remaining : MIA_VIDEO_RECORDS_PER_CHUNK;
    uint16_t payload_len = (uint16_t)(record_count * MIA_VIDEO_PAGE_RECORD_SIZE);

    uint16_t packet_len = video_build_header(
        MIA_VIDEO_PACKET_FRAME_DATA,
        video_session.session_id,
        video_response.frame_id,
        video_response.request_id,
        chunk_index,
        video_response.chunk_count,
        payload_len);

    uint32_t out = MIA_VIDEO_HEADER_SIZE;
    for (uint32_t i = 0; i < record_count; i++) {
        uint16_t page = video_pending_pages[first_record + i];
        uint32_t page_offset = (uint32_t)page << MIA_VIDEO_PAGE_SHIFT;
        uint32_t valid_len = MIA_VIDEO_PAGE_SIZE;

        if (page_offset + valid_len > MIA_VIDEO_STATE_SIZE) {
            valid_len = MIA_VIDEO_STATE_SIZE - page_offset;
        }

        mia_video_write_u16(&video_tx_packet[out], page);
        out += 2;
        memset(&video_tx_packet[out], 0, MIA_VIDEO_PAGE_SIZE);
        memcpy(&video_tx_packet[out], &mem[page_offset], valid_len);
        out += MIA_VIDEO_PAGE_SIZE;
    }

    return video_send_packet(video_tx_packet, packet_len, &video_session.addr, video_session.port);
}

static bool video_validate_hello(const mia_video_header_t *header) {
    return header->payload_len == 0 &&
           header->session_id == 0 &&
           header->frame_id == 0 &&
           header->request_id == 0 &&
           header->chunk_index == 0 &&
           header->chunk_count == 0;
}

static bool video_accepts_session_packet(const mia_video_header_t *header, const ip_addr_t *addr, uint16_t port) {
    if (!video_session.active || header->session_id != video_session.session_id) {
        return false;
    }

    return video_session.port == port && ip_addr_cmp(&video_session.addr, addr);
}

static void video_reset_session(const ip_addr_t *addr, uint16_t port) {
    video_clear_dirty_maps();
    mia_video_active_dirty_index = 0;
    video_release_pending_response(false);
    memset(&video_response, 0, sizeof(video_response));
    video_repair_count = 0;
    video_repair_pos = 0;

    video_session.active = true;
    ip_addr_copy(video_session.addr, *addr);
    video_session.port = port;
    video_session.session_id = time_us_32() ^ ((uint32_t)port << 16) ^ video_next_seq_value ^ 0x9E3779B9u;
    if (video_session.session_id == 0) {
        video_session.session_id = 1;
    }
    video_session.client_frame_id = 0;
    video_frame_id = 0;
    video_set_frame_id(0);
    video_set_last_response_dirty_pages(0);
    mia_status_clear_flag(MIA_STAT_VIDEO_FRAME_REQUESTED | MIA_STAT_VIDEO_FRAME_SENT);
    mia_video_force_full_refresh();
}

static void video_invalidate_session(void) {
    video_clear_dirty_maps();
    video_release_pending_response(false);
    memset(&video_response, 0, sizeof(video_response));
    memset(&video_session, 0, sizeof(video_session));
    video_repair_count = 0;
    video_repair_pos = 0;
    mia_video_active_dirty_index = 0;
    mia_status_clear_flag(MIA_STAT_VIDEO_FRAME_REQUESTED | MIA_STAT_VIDEO_FRAME_SENT);
}

static void video_release_pending_response(bool acknowledged) {
    if (!video_response.valid) {
        return;
    }

    uint32_t frame_id = video_response.frame_id;
    video_clear_dirty_map(video_response.map_index);
    video_response.valid = false;
    video_response.page_count = 0;
    video_response.chunk_count = 0;
    video_response.next_chunk_to_send = 0;
    video_response.initial_send_done = false;
    video_repair_count = 0;
    video_repair_pos = 0;

    if (acknowledged) {
        video_session.client_frame_id = frame_id;
        mia_status_clear_flag(MIA_STAT_VIDEO_FRAME_REQUESTED | MIA_STAT_VIDEO_FRAME_SENT);
        mia_irq_set_flag(IRQ_VIDEO_FRAME_ACKED);
    }
}

static void video_protocol_error(const ip_addr_t *addr, uint16_t port, uint16_t request_id, uint32_t frame_id) {
    if (!video_session.active) {
        return;
    }

    uint32_t session_id = video_session.session_id;
    uint16_t packet_len = video_build_header(
        MIA_VIDEO_PACKET_STATUS,
        session_id,
        frame_id,
        request_id,
        0,
        0,
        2);
    mia_video_write_u16(&video_tx_packet[MIA_VIDEO_HEADER_SIZE], MIA_VIDEO_STATUS_PROTOCOL_ERROR);
    video_invalidate_session();
    (void)video_send_packet(video_tx_packet, packet_len, addr, port);
}

static void video_handle_datagram(const uint8_t *packet, uint16_t packet_len, const ip_addr_t *addr, uint16_t port) {
    mia_video_header_t header;
    if (!mia_video_parse_header(packet, packet_len, &header)) {
        return;
    }

    const uint8_t *payload = &packet[MIA_VIDEO_HEADER_SIZE];

    if (header.packet_type == MIA_VIDEO_PACKET_HELLO) {
        if (!video_validate_hello(&header)) {
            video_protocol_error(addr, port, header.request_id, header.frame_id);
            return;
        }

        video_last_peer_seq = header.seq;
        video_reset_session(addr, port);
        (void)video_send_welcome(addr, port);
        return;
    }

    if (!video_accepts_session_packet(&header, addr, port)) {
        return;
    }

    video_last_peer_seq = header.seq;

    switch (header.packet_type) {
        case MIA_VIDEO_PACKET_REQUEST_FRAME:
            video_handle_request_frame(&header, payload, addr, port);
            break;
        case MIA_VIDEO_PACKET_ACK_RESPONSE:
            video_handle_ack_response(&header, addr, port);
            break;
        case MIA_VIDEO_PACKET_NACK_CHUNKS:
            video_handle_nack_chunks(&header, payload, addr, port);
            break;
        case MIA_VIDEO_PACKET_STATUS:
            video_handle_client_status(&header, payload);
            break;
        default:
            video_protocol_error(addr, port, header.request_id, header.frame_id);
            break;
    }
}

static void video_handle_request_frame(const mia_video_header_t *header, const uint8_t *payload, const ip_addr_t *addr, uint16_t port) {
    if (header->payload_len != 4 || header->frame_id != 0 || header->chunk_index != 0 || header->chunk_count != 0) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
        return;
    }

    uint32_t last_complete = mia_video_read_u32(payload);

    if (video_response.valid) {
        if (header->request_id == video_response.request_id && last_complete == video_response.last_complete_frame_id) {
            video_response.next_chunk_to_send = 0;
            return;
        }

        if (last_complete == video_response.frame_id) {
            video_release_pending_response(true);
            video_handle_request_frame_no_pending(header->request_id, last_complete, addr, port);
            return;
        }

        if (last_complete > video_response.frame_id) {
            video_protocol_error(addr, port, header->request_id, header->frame_id);
            return;
        }

        return;
    }

    if (last_complete < video_session.client_frame_id) {
        return;
    }

    if (last_complete > video_session.client_frame_id) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
        return;
    }

    video_handle_request_frame_no_pending(header->request_id, last_complete, addr, port);
}

static void video_handle_request_frame_no_pending(uint16_t request_id, uint32_t last_complete, const ip_addr_t *addr, uint16_t port) {
    if (!video_has_dirty_pages(mia_video_active_dirty_index)) {
        video_set_last_response_dirty_pages(0);
        (void)video_send_status(MIA_VIDEO_STATUS_NO_DIRTY_PAGES, request_id, 0, addr, port);
        return;
    }

    video_request_dirty_rotation();

    video_response.map_index = mia_video_rotated_pending_index;
    video_response.page_count = video_scan_dirty_pages(video_response.map_index);
    video_response.chunk_count = (uint16_t)((video_response.page_count + MIA_VIDEO_RECORDS_PER_CHUNK - 1u) / MIA_VIDEO_RECORDS_PER_CHUNK);
    video_response.frame_id = video_next_frame_id();
    video_response.request_id = request_id;
    video_response.last_complete_frame_id = last_complete;
    video_response.next_chunk_to_send = 0;
    video_response.initial_send_done = false;
    video_response.valid = true;
    video_repair_count = 0;
    video_repair_pos = 0;

    video_set_frame_id(video_response.frame_id);
    mia_status_set_flag(MIA_STAT_VIDEO_FRAME_REQUESTED);
    mia_irq_set_flag(IRQ_VIDEO_FRAME_REQUEST);
}

static void video_handle_ack_response(const mia_video_header_t *header, const ip_addr_t *addr, uint16_t port) {
    if (header->payload_len != 0 || header->chunk_index != 0 || header->chunk_count != 0) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
        return;
    }

    if (video_response.valid) {
        if (header->request_id == video_response.request_id && header->frame_id == video_response.frame_id) {
            video_release_pending_response(true);
            return;
        }

        if (header->frame_id > video_response.frame_id) {
            video_protocol_error(addr, port, header->request_id, header->frame_id);
        }

        return;
    }

    if (header->frame_id > video_session.client_frame_id) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
    }
}

static void video_handle_nack_chunks(const mia_video_header_t *header, const uint8_t *payload, const ip_addr_t *addr, uint16_t port) {
    if (header->chunk_index != 0 || header->chunk_count != 0 || header->payload_len < 4) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
        return;
    }

    uint16_t missing_count = mia_video_read_u16(&payload[0]);
    uint16_t reserved = mia_video_read_u16(&payload[2]);
    if (reserved != 0 ||
        missing_count == 0 ||
        missing_count > MIA_VIDEO_MAX_CHUNKS ||
        header->payload_len != (uint16_t)(4u + missing_count * 2u)) {
        video_protocol_error(addr, port, header->request_id, header->frame_id);
        return;
    }

    if (!video_response.valid) {
        if (header->frame_id > video_session.client_frame_id) {
            video_protocol_error(addr, port, header->request_id, header->frame_id);
        }
        return;
    }

    if (header->request_id != video_response.request_id || header->frame_id != video_response.frame_id) {
        if (header->frame_id > video_response.frame_id) {
            video_protocol_error(addr, port, header->request_id, header->frame_id);
        }
        return;
    }

    bool seen[MIA_VIDEO_MAX_CHUNKS];
    memset(seen, 0, sizeof(seen));

    for (uint16_t i = 0; i < missing_count; i++) {
        uint16_t chunk = mia_video_read_u16(&payload[4u + i * 2u]);
        if (chunk >= video_response.chunk_count || seen[chunk]) {
            video_protocol_error(addr, port, header->request_id, header->frame_id);
            return;
        }

        seen[chunk] = true;
        video_repair_chunks[i] = chunk;
    }

    video_repair_count = missing_count;
    video_repair_pos = 0;
}

static void video_handle_client_status(const mia_video_header_t *header, const uint8_t *payload) {
    if (header->payload_len != 2 || header->chunk_index != 0 || header->chunk_count != 0) {
        video_invalidate_session();
        return;
    }

    uint16_t status = mia_video_read_u16(payload);
    if (status == MIA_VIDEO_STATUS_PROTOCOL_ERROR) {
        video_invalidate_session();
    }
}

static void video_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;

    if (p == NULL) {
        return;
    }

    if (p->tot_len <= MIA_VIDEO_UDP_PAYLOAD_SIZE) {
        pbuf_copy_partial(p, video_rx_packet, p->tot_len, 0);
        video_handle_datagram(video_rx_packet, p->tot_len, addr, port);
    }

    pbuf_free(p);
}
