#ifndef _MIA_VIDEO_PACKETS_H_
#define _MIA_VIDEO_PACKETS_H_

#include <stdbool.h>
#include <stdint.h>

#include "video_dirty.h"

#define MIA_VIDEO_MAGIC 0x4D56u
#define MIA_VIDEO_VERSION 1u

#define MIA_VIDEO_PACKET_HELLO 0x01u
#define MIA_VIDEO_PACKET_WELCOME 0x02u
#define MIA_VIDEO_PACKET_REQUEST_FRAME 0x05u
#define MIA_VIDEO_PACKET_ACK_RESPONSE 0x06u
#define MIA_VIDEO_PACKET_NACK_CHUNKS 0x07u
#define MIA_VIDEO_PACKET_FRAME_DATA 0x20u
#define MIA_VIDEO_PACKET_STATUS 0x30u

#define MIA_VIDEO_STATUS_NO_DIRTY_PAGES 0u
#define MIA_VIDEO_STATUS_PROTOCOL_ERROR 1u

typedef struct {
    uint8_t packet_type;
    uint32_t session_id;
    uint32_t seq;
    uint32_t ack;
    uint32_t frame_id;
    uint16_t request_id;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t payload_len;
} mia_video_header_t;

static inline uint16_t mia_video_read_u16(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline uint32_t mia_video_read_u32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static inline void mia_video_write_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static inline void mia_video_write_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static inline bool mia_video_parse_header(const uint8_t *packet, uint16_t packet_len, mia_video_header_t *header) {
    if (packet_len < MIA_VIDEO_HEADER_SIZE) {
        return false;
    }

    if (mia_video_read_u16(&packet[0]) != MIA_VIDEO_MAGIC || packet[2] != MIA_VIDEO_VERSION) {
        return false;
    }

    uint16_t payload_len = mia_video_read_u16(&packet[26]);
    if ((uint16_t)(packet_len - MIA_VIDEO_HEADER_SIZE) != payload_len) {
        return false;
    }

    if (mia_video_read_u16(&packet[28]) != 0 || mia_video_read_u16(&packet[30]) != 0) {
        return false;
    }

    header->packet_type = packet[3];
    header->session_id = mia_video_read_u32(&packet[4]);
    header->seq = mia_video_read_u32(&packet[8]);
    header->ack = mia_video_read_u32(&packet[12]);
    header->frame_id = mia_video_read_u32(&packet[16]);
    header->request_id = mia_video_read_u16(&packet[20]);
    header->chunk_index = mia_video_read_u16(&packet[22]);
    header->chunk_count = mia_video_read_u16(&packet[24]);
    header->payload_len = payload_len;

    return true;
}

#endif
