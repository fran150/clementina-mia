# MIA Video Implementation Plan

This document is a MIA-firmware implementation plan for the video protocol
defined in [video-protocol.md](video-protocol.md). It covers the MIA side only:
6502-facing registers/commands, dirty tracking, UDP session handling,
dirty-page response generation, repair, status bits, IRQ events, and integration
with the existing core 0/core 1 firmware split.

The current protocol is client-paced and has exactly one outstanding update
response. There is no 6502 frame commit command, no multiple in-flight response
queue, no retained frame snapshots, and no copy-on-write page versioning.

The core mechanism is dirty-page tracking. MIA treats the first 68,944 bytes of
its RAM as video state, divides that region into 2,155 pages of 32 bytes each,
and tracks page changes with a 270-byte dirty map. Each bit in the map names one
video page. Core 1 sets the bit for the page it just wrote; core 0 later uses a
captured dirty map to decide which pages to send to the client.

## Design Constraints

- Core 1 owns the time-critical 6502 bus action loop.
- Core 1 must not build packets, scan dirty maps, allocate memory, poll Wi-Fi,
  or wait for core 0.
- A video-memory write on core 1 should remain bounded: write one byte to MIA
  RAM and mark one dirty bit.
- Core 0 owns all slow video work: dirty-map rotation, page list generation,
  packet construction, UDP send/receive, repair, bandwidth pacing, status, and
  lifecycle events.
- Update responses read live MIA RAM. The pending dirty map freezes the list of
  pages for a response, not the byte values in those pages.
- Repair regenerates chunks from the retained pending dirty map using the same
  deterministic page order.
- Full refresh is implemented by marking every video page dirty.

## Proposed Source Layout

Add a video subsystem under `src/mia/video/`:

| File | Purpose |
| --- | --- |
| `video.h` | public init/service APIs, constants, and core 1 dirty-mark helper |
| `video.c` | protocol state machine, UDP receive/send, dirty-map rotation, response lifecycle |
| `video_packets.h` | packet constants, packed wire structs, validation helpers |
| `video_packets.c` | packet encode/decode helpers and checksum-free UDP payload validation |
| `video_dirty.h` | dirty-map constants and inline bit helpers |
| `video_dirty.c` | core 0 dirty-map scan/page-list helpers |

Add those files to `CMakeLists.txt` and include `src/mia/video` in the include
paths.

Keep the core 1 helper small enough to inline from `mem/indexes.h`. Put only
constants and `static inline` dirty-mark code in headers included by the hot
path.

## Core Constants

Define protocol and memory constants once, preferably in `video_dirty.h` or a
shared `video_layout.h`:

```c
#define MIA_VIDEO_STATE_SIZE       68944u
#define MIA_VIDEO_PAGE_SIZE        32u
#define MIA_VIDEO_PAGE_SHIFT       5u
#define MIA_VIDEO_PAGE_COUNT       2155u
#define MIA_VIDEO_DIRTY_MAP_SIZE   270u
#define MIA_VIDEO_PAGE_RECORD_SIZE 34u
#define MIA_VIDEO_HEADER_SIZE      32u
#define MIA_VIDEO_DEFAULT_PAYLOAD  512u
```

The final page has only 16 valid bytes. Packet generation still sends 32 bytes
for that page; the client ignores the padding.

## Control Block Integration

The first 256 bytes of video RAM are the video control block. Add named offsets
or a carefully packed access layer for:

| Offset | Field |
| ---: | --- |
| `$00` | `VIDEO_VERSION` |
| `$01` | `VIDEO_MODE` |
| `$02` | `VIDEO_STATUS` |
| `$03` | `LAYER_ENABLE` |
| `$04-$07` | `FRAME_ID` |
| `$18` | `VIDEO_IRQ_ENABLE` |
| `$19` | `VIDEO_EVENT_STATUS` |

Status bits:

```c
#define VIDEO_CLIENT_CONNECTED      (1u << 0)
#define VIDEO_UPDATE_ACTIVE         (1u << 1)
#define VIDEO_READOUT_ACTIVE        (1u << 2)
#define VIDEO_RESPONSE_SENT         (1u << 3)
#define VIDEO_FULL_REFRESH_PENDING  (1u << 4)
#define VIDEO_REPAIR_ACTIVE         (1u << 5)
```

Event bits:

```c
#define VIDEO_EVENT_FRAME_REQUEST   (1u << 0)
#define VIDEO_EVENT_FRAME_SENT      (1u << 1)
#define VIDEO_EVENT_FRAME_ACKED     (1u << 2)
#define VIDEO_EVENT_CLIENT_CHANGE   (1u << 3)
#define VIDEO_EVENT_FULL_REFRESH    (1u << 4)
#define VIDEO_EVENT_REPAIR_REQUEST  (1u << 5)
#define VIDEO_EVENT_READOUT_START   (1u << 6)
#define VIDEO_EVENT_READOUT_END     (1u << 7)
```

Add `IRQ_VIDEO_EVENT` to [src/mia/irq/irq.h](../src/mia/irq/irq.h). The docs
currently reserve `$0020`, so the next bit after `IRQ_SPEED_CHANGED` is:

```c
#define IRQ_VIDEO_EVENT (1u << 5)
```

Implement a helper that latches video events, evaluates
`VIDEO_IRQ_ENABLE`, and sets `IRQ_VIDEO_EVENT` in the normal `IRQ_STATUS` when
at least one enabled video event is pending.

## Commands

Add video command handlers in the command table:

| Command | Id | Core 0 handler |
| --- | ---: | --- |
| `VIDEO_ENABLE` | `$40` | initialize video control state and fast video indexes |
| `VIDEO_FORCE_FULL_REFRESH` | `$42` | schedule a full refresh for the next update |
| `VIDEO_SET_MODE` | `$43` | update `VIDEO_MODE` bits |

Suggested command behavior:

- `VIDEO_ENABLE` clears video state, initializes the video control block,
  configures fast video index descriptors, clears both dirty maps, and sets
  `VIDEO_FULL_REFRESH_PENDING`.
- `VIDEO_FORCE_FULL_REFRESH` sets `VIDEO_FULL_REFRESH_PENDING` and raises
  `VIDEO_EVENT_FULL_REFRESH`. The next accepted response marks every pending
  page dirty after dirty-map rotation.
- `VIDEO_SET_MODE` updates the `VIDEO_MODE` field and can trigger full refresh
  if a mode change invalidates the client renderer's assumptions.

Do not implement `VIDEO_COMMIT_FRAME`; it belongs to the older protocol model.

## Fast Video Index Setup

`VIDEO_ENABLE` should configure the fast indexes listed in
[video-output.md](video-output.md):

| Index | Range | Length |
| ---: | ---: | ---: |
| `$80` | `$00008-$00009` | 2 |
| `$81` | `$0000A-$0000B` | 2 |
| `$82` | `$0000C-$0000D` | 2 |
| `$83` | `$0000E-$00012` | 5 |
| `$84` | `$00003-$00003` | 1 |
| `$85` | `$00014-$00015` | 2 |
| `$86` | `$00016-$00016` | 1 |
| `$87` | `$00002-$00002` | 1 |
| `$88` | `$00100-$001FF` | 256 |
| `$89` | `$10850-$10D4F` | 1,280 |
| `$8A` | `$10080-$10467` | 1,000 |
| `$8B` | `$10468-$1084F` | 1,000 |
| `$90-$97` | CHR banks | 6,144 each |
| `$A0-$A7` | background nametables | 1,000 each |
| `$A8-$AF` | background attributes | 1,000 each |

All fast indexes should use step-on-write and wrap. `VIDX_VIDEO_STATUS` is a
read index and should expose the current `VIDEO_STATUS`.

## Dirty Tracking Hot Path

Add two dirty maps owned by the video subsystem:

```c
static uint8_t video_dirty_maps[2][MIA_VIDEO_DIRTY_MAP_SIZE];
static volatile uint8_t video_active_dirty_index;
static uint8_t video_pending_dirty_index;
static bool video_pending_valid;
```

There is no third dirty map and no explicit free-map flag. When
`video_pending_valid` is false, the non-active map is the available map and must
already be clear:

```c
uint8_t available_idx = video_active_dirty_index ^ 1u;
```

Core 1 only writes the active dirty map. Core 0 scans only the pending map. Core
0 clears only maps that core 1 cannot write: both maps during startup/reset, or
the pending map after a matching ACK has been validated and before that pending
state is released. Once `video_pending_valid` becomes false, that cleared
non-active map is the available map for the next rotation.

The safest shape for the hot path is an active-map index:

```c
extern volatile uint8_t video_active_dirty_index;
extern uint8_t video_dirty_maps[2][MIA_VIDEO_DIRTY_MAP_SIZE];

static inline void __not_in_flash_func(mia_video_mark_dirty)(uint32_t addr) {
    if (addr >= MIA_VIDEO_STATE_SIZE) {
        return;
    }

    uint32_t page = addr >> MIA_VIDEO_PAGE_SHIFT;
    uint8_t *dirty = video_dirty_maps[video_active_dirty_index];
    dirty[page >> 3] |= (uint8_t)(1u << (page & 7u));
}
```

Dirty-map rotation must be core-1-safe. Core 0 should request a rotation, then
wait for core 1 to perform it between bus actions. That ensures no dirty mark is
in flight to the old active map when core 0 starts scanning it:

```c
// Core 0, after accepting REQUEST_FRAME and deciding a response is needed.
video_rotate_request = true;

// Core 1, between bus/write actions.
if (video_rotate_request) {
    uint8_t old_active = video_active_dirty_index;
    video_active_dirty_index = old_active ^ 1u; // target map is already clear
    video_rotated_pending_index = old_active;
    video_rotate_request = false;
    video_rotate_done = true;
}

// Core 0, after video_rotate_done.
video_pending_dirty_index = video_rotated_pending_index;
video_pending_valid = true;
```

Core 0 must not clear the new active map after this rotation. Core 1 may set a
dirty bit there immediately after it switches indexes.

Then update the write helpers in [src/mia/mem/indexes.h](../src/mia/mem/indexes.h):

- In `index_write()`, mark dirty after writing the byte.
- In `index_write_and_step()`, capture the pre-step address, write the byte,
  mark dirty for that address, then step the index.

The dirty mark must use the actual write address before wrapping/stepping.

Avoid touching dirty maps for writes outside the video state range. This keeps
general MIA RAM and index descriptor traffic from creating video updates.

## Video Lifecycle Events

The dirty maps connect two independent event streams:

- core 1 receives 6502 bus writes and marks video pages dirty;
- core 0 receives UDP packets from the client and turns dirty pages into update
  responses.

The lifecycle is:

| Event | Meaning | Dirty-map action | 6502-visible state |
| --- | --- | --- | --- |
| 6502 writes video memory | one byte in live video RAM changed | set one bit in the active dirty map | no status event; the write is only queued for a future update |
| client connects or recovery is required | client mirror must be rebuilt | set `VIDEO_FULL_REFRESH_PENDING`; next response marks every pending page dirty | `VIDEO_FULL_REFRESH_PENDING` and `VIDEO_EVENT_FULL_REFRESH` are set |
| client sends `REQUEST_FRAME` and no response is pending | client asks MIA to publish accumulated changes | rotate active dirty map to pending; already-clear other map becomes active | `VIDEO_UPDATE_ACTIVE`, `VIDEO_READOUT_ACTIVE`, `VIDEO_EVENT_FRAME_REQUEST`, and `VIDEO_EVENT_READOUT_START` are set |
| first response send completes | every dirty page chunk was sent once | keep pending map for possible repair | clear `VIDEO_READOUT_ACTIVE`, set `VIDEO_RESPONSE_SENT`, latch sent/end events |
| client sends `NACK_CHUNKS` | client missed one or more chunks | regenerate those chunks from the pending map | set repair/readout bits while repair is being generated |
| client sends `ACK_RESPONSE` | client applied the complete update | clear pending map, release pending state, and advance acknowledged frame id | clear update/sent bits and latch `VIDEO_EVENT_FRAME_ACKED` after cleanup |

This event model defines the write timing rules. Writes before an accepted
`REQUEST_FRAME` are named by the pending map and are intended for that update.
Writes after the rotation go into the new active map for the next update. Because
MIA reads live RAM, visible writes during `VIDEO_READOUT_ACTIVE` can still be
read into the current response. Visible writes after `VIDEO_RESPONSE_SENT` but
before `ACK_RESPONSE` can appear in repair chunks for the same response. A
program that wants strict visual cleanliness waits until
`VIDEO_EVENT_FRAME_ACKED` or until `VIDEO_UPDATE_ACTIVE` is clear before writing
visible video memory.

## Core 0 Dirty-Map Rotation

When a valid `REQUEST_FRAME` arrives and no response is pending:

1. If `VIDEO_FULL_REFRESH_PENDING` is not set and the active dirty map is empty,
   send `STATUS(NO_DIRTY_PAGES)` and do not assign a new frame id.
2. Derive the available map as `active_idx ^ 1`. It must already be clear.
3. Request a core-1-safe rotation and wait for `video_rotate_done`.
4. Set `video_pending_dirty_index` to the old active index and
   `video_pending_valid = true`.
5. If `VIDEO_FULL_REFRESH_PENDING` is set, set every valid page bit in the
   pending dirty map, clear padding bits beyond page 2,154, and clear
   `VIDEO_FULL_REFRESH_PENDING`.
6. Assign the next nonzero `FRAME_ID`.
7. Set `VIDEO_UPDATE_ACTIVE` and `VIDEO_READOUT_ACTIVE`.
8. Latch `VIDEO_EVENT_FRAME_REQUEST` and `VIDEO_EVENT_READOUT_START`.
9. Scan the pending dirty map into a page index list.

The pending dirty map must not be modified until the pending response is
acknowledged, implicitly acknowledged, or the session expires.

For fast packet generation and repair, build:

```c
static uint16_t pending_pages[MIA_VIDEO_PAGE_COUNT];
static uint16_t pending_page_count;
```

Worst case list size is 4,310 bytes. This avoids repeated bitmap scans during
send and repair.

## Dirty-Map Scan

Scan the 270-byte map using byte skips and count-trailing-zero logic:

```c
for (uint32_t byte_i = 0; byte_i < MIA_VIDEO_DIRTY_MAP_SIZE; ++byte_i) {
    uint8_t bits = pending_dirty[byte_i];
    while (bits != 0) {
        uint32_t bit = __builtin_ctz(bits);
        uint32_t page = byte_i * 8u + bit;
        if (page < MIA_VIDEO_PAGE_COUNT) {
            pending_pages[pending_page_count++] = (uint16_t)page;
        }
        bits &= (uint8_t)(bits - 1u);
    }
}
```

This scan is core 0 work. Even at 30 FPS the raw bitmap scan is tiny compared
with UDP send time.

## UDP Integration

Add `mia_video_init()` and `mia_video_service()`:

```c
void mia_video_init(void);
void mia_video_service(void);
```

Integrate them in normal-mode initialization and the main loop:

```c
mia_video_init();

while (true) {
    mia_handle_reset_request();
    mia_service();
    cyw43_arch_poll();
    mia_video_service();
    update_onboard_led_blink();
    tight_loop_contents();
}
```

The project already links `pico_cyw43_arch_lwip_poll`. The current
[src/mia/main.c](../src/mia/main.c) loop does not call `cyw43_arch_poll()`, so
Wi-Fi polling must be added when UDP is implemented.

Plan UDP receive with lwIP `udp_pcb`:

- Bind to the chosen video UDP port.
- Receive packets into a small stack/local decode buffer.
- Validate the 32-byte protocol header before reading payload fields.
- Dispatch `HELLO`, `SET_PARAMS`, `REQUEST_FRAME`, `ACK_RESPONSE`,
  `NACK_CHUNKS`, and optional client `STATUS`.

Plan UDP send with bounded per-service work:

- Send at most a small number of chunks per `mia_video_service()` call.
- Respect pbuf allocation failure by retrying later.
- Respect accepted bandwidth hint with a token bucket.
- Avoid blocking on Wi-Fi or pbuf availability.

## Session State

Use one active client endpoint:

```c
typedef struct {
    bool active;
    ip_addr_t addr;
    uint16_t port;
    uint32_t session_id;
    uint32_t last_rx_ms;
    uint32_t client_frame_id;
    uint32_t latest_frame_id;
} mia_video_session_t;
```

Session rules:

- `HELLO` with no active session creates a new session and returns `WELCOME`.
- `HELLO` from a different endpoint while active returns `STATUS(BUSY)`.
- Session id `0` is only valid for `HELLO` and no-session status.
- Valid packets refresh `last_rx_ms`.
- Expiry after 3,000 ms releases pending response state and clears
  `VIDEO_CLIENT_CONNECTED`.
- A new session marks all pages dirty for full refresh.

Generate nonzero `session_id` from available entropy or a mixed timer/counter
fallback. Avoid assigning `0`.

## Parameter State

Accepted parameters:

```c
typedef struct {
    uint8_t target_fps;          // 5-30
    uint16_t max_payload;        // 256-512
    uint16_t repair_timeout_ms;  // 30-500
    uint16_t bandwidth_kib_s;    // 0 or 16-4096
} mia_video_params_t;
```

`SET_PARAMS` validates reserved payload bytes, clamps values, replies with
`PARAMS_ACCEPTED`, and sets `PARAMS_CLAMPED` when needed.

Precompute:

```c
records_per_chunk = (max_payload - MIA_VIDEO_HEADER_SIZE) /
                    MIA_VIDEO_PAGE_RECORD_SIZE;
```

Reject or clamp any payload size that would make `records_per_chunk == 0`.

## Pending Response State

Because v1 has one outstanding response, keep one pending response object:

```c
typedef enum {
    VIDEO_RESP_NONE,
    VIDEO_RESP_SENDING,
    VIDEO_RESP_SENT_WAIT_ACK,
    VIDEO_RESP_REPAIRING
} mia_video_response_phase_t;

typedef struct {
    mia_video_response_phase_t phase;
    uint16_t request_id;
    uint32_t frame_id;
    uint32_t base_client_frame_id;
    uint16_t page_count;
    uint16_t chunk_count;
    uint16_t next_chunk_to_send;
    uint16_t records_per_chunk;
    bool initial_send_done;
} mia_video_response_t;
```

The pending response owns:

- the pending dirty map,
- `pending_pages[]`,
- `page_count`,
- `chunk_count`,
- request id and frame id.

On ACK:

- Require `request_id` and `frame_id` to match the pending response.
- Clear `video_dirty_maps[video_pending_dirty_index]`.
- Release pending state by setting `video_pending_valid = false`.
- Set `client_frame_id = frame_id`.
- Clear `VIDEO_UPDATE_ACTIVE`, `VIDEO_RESPONSE_SENT`, `VIDEO_READOUT_ACTIVE`,
  and `VIDEO_REPAIR_ACTIVE`.
- Latch `VIDEO_EVENT_FRAME_ACKED`.

The pending dirty map must be cleared before status bits are cleared, before
`VIDEO_EVENT_FRAME_ACKED` is latched, and before `IRQ_VIDEO_EVENT` is evaluated.
After the event is visible to the 6502, the non-active map is already clear and
ready for the next `REQUEST_FRAME` rotation.

Duplicate/old/unknown ACKs are ignored. Future/impossible ACKs are protocol
errors and force full refresh.

## Packet Encoding

Use packed structs only for fixed-size headers, and still validate all fields
before use:

```c
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t session_id;
    uint32_t seq;
    uint32_t ack;
    uint32_t frame_id;
    uint16_t request_id;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t payload_len;
    uint16_t flags;
    uint16_t reserved;
} mia_video_header_t;
```

All multi-byte fields are little-endian. Pico is little-endian, but keep helper
functions so packet code is explicit and testable.

For each `FRAME_DATA` chunk:

1. Compute `first_record = chunk_index * records_per_chunk`.
2. Compute `record_count = min(records_per_chunk, page_count - first_record)`.
3. For each record:
   - page index = `pending_pages[first_record + i]`;
   - offset = `page_index * 32`;
   - write little-endian `page_index`;
   - copy 32 bytes from `mem[offset]`, padding the final page beyond valid
     video state with zero.
4. Send UDP packet with `payload_len = record_count * 34`.

Do not include clean pages. Do not merge ranges or use data-dependent fill
compression in v1; deterministic fixed-size page records make repair simple.

## Request Handling

`REQUEST_FRAME` cases:

| State | Request | MIA action |
| --- | --- | --- |
| no pending | `last_complete == client_frame_id` | accept request or return `NO_DIRTY_PAGES` |
| no pending | `last_complete < client_frame_id` | ignore stale request |
| no pending | `last_complete > client_frame_id` | protocol error, schedule full refresh |
| pending | same `request_id` and same base | resend/regenerate pending response |
| pending | `last_complete == pending.frame_id` | implicit ACK, then handle request |
| pending | `last_complete > pending.frame_id` | protocol error, schedule full refresh |
| pending | other stale/duplicate/early request | return `RESPONSE_PENDING` |

`STATUS(NO_DIRTY_PAGES)` does not assign a frame id and does not require ACK.

When accepting a request with dirty pages:

- increment nonzero `FRAME_ID`;
- update the control block `FRAME_ID`;
- create the pending response;
- send chunks over successive service calls.

## Repair Handling

`NACK_CHUNKS` cases:

| Packet | MIA action |
| --- | --- |
| matches pending response and indexes valid | regenerate/send requested chunks |
| old/duplicate/unknown response | ignore |
| future/impossible response | protocol error, schedule full refresh |
| malformed payload/index list | protocol error |

Repair lifecycle:

1. Latch `VIDEO_EVENT_REPAIR_REQUEST`.
2. Set `VIDEO_REPAIR_ACTIVE` and `VIDEO_READOUT_ACTIVE`.
3. Latch `VIDEO_EVENT_READOUT_START`.
4. Send requested chunks using the pending page list and current RAM values.
5. Clear `VIDEO_REPAIR_ACTIVE` and `VIDEO_READOUT_ACTIVE`.
6. Latch `VIDEO_EVENT_READOUT_END`.

Repair does not release pending response state. Only ACK or session expiry
does.

## Full Refresh Handling

Implement:

```c
void mia_video_force_full_refresh(void);
```

It should:

- set `VIDEO_FULL_REFRESH_PENDING`;
- latch `VIDEO_EVENT_FULL_REFRESH`;
- leave pending response state alone unless called as part of protocol-error
  recovery.

It should not set all bits in the active dirty map. Core 1 can write the active
map at any time. Instead, the next accepted `REQUEST_FRAME` rotates the active
map into the pending role and then sets every valid page bit in that pending
map, where core 1 can no longer modify it.

For protocol-error recovery:

1. discard pending response state if any;
2. clear the old pending dirty map before releasing it;
3. set `video_pending_valid = false`;
4. set `VIDEO_FULL_REFRESH_PENDING`;
5. send `STATUS(PROTOCOL_ERROR)` if useful.

The next valid `REQUEST_FRAME` generates a full refresh response.

## Status and Event Helpers

Create helpers instead of open-coding bit twiddling:

```c
void mia_video_status_set(uint8_t bits);
void mia_video_status_clear(uint8_t bits);
void mia_video_event_latch(uint8_t bits);
```

`mia_video_event_latch()` should:

- OR bits into `VIDEO_EVENT_STATUS`;
- if `(VIDEO_EVENT_STATUS & VIDEO_IRQ_ENABLE) != 0`, set `IRQ_VIDEO_EVENT`;
- otherwise clear `IRQ_VIDEO_EVENT`;
- call `mia_irq_eval()`.

When the 6502 writes `1` bits to `VIDEO_EVENT_STATUS`, clear those bits and
reevaluate `IRQ_VIDEO_EVENT` with the same rule. This write-clear behavior needs
either a hook in the indexed write path for the control block event byte or a
command/helper that normalizes writes to that offset.

## Core 1 Hook Details

The current core 1 normal-mode write path calls:

- `index_write_and_step()` when the 6502 writes `$FFE0` or `$FFE4`;
- `index_write()` from command/helpers in some paths.

Implementation steps:

1. Include the smallest video dirty header in `mem/indexes.h`.
2. In `index_write_and_step()`, save `write_addr = entry->current_addr` before
   the write and step.
3. Write `mem[write_addr & MIA_RAM_MASK] = value`.
4. Call `mia_video_mark_dirty(write_addr & MIA_RAM_MASK)`.
5. Continue existing index step/wrap logic.
6. In `index_write()`, mark dirty using the current address.

If dirty marking causes measurable hot-path cost, optimize in this order:

- keep `MIA_VIDEO_STATE_SIZE` as an immediate constant;
- use an active map index rather than pointer-chasing;
- align dirty maps;
- make the helper `__not_in_flash_func` and force-inline;
- consider marking only for fast-video index ids if general video range checks
  are too expensive.

Do not add locks, FIFO messages, or core 0 calls to this path.

## Control Register Write-Clear Caveat

The video control block lives in MIA RAM, so 6502 writes to
`VIDEO_EVENT_STATUS` are ordinary indexed writes. The desired semantics are
"write 1 to clear" for event bits, not "store this byte literally."

Plan one of these:

1. In `mia_video_mark_dirty()` or a nearby write hook, detect writes to offset
   `$19` and translate them into write-1-clear behavior.
2. Provide a fast video index or command specifically for clearing video events.
3. Accept literal RAM writes for the first bring-up, then add write-1-clear
   before documenting the behavior as complete.

Option 1 is most transparent to programmers but adds a branch to the hot path.
Option 2 avoids the hot-path branch but is less memory-like. Decide during
implementation profiling.

## Bandwidth Pacing

Implement the token bucket in core 0:

- bucket units are application UDP payload bytes, including the 32-byte header;
- `bandwidth_kib_s = 0` disables pacing;
- refill based on elapsed time in `mia_video_service()`;
- before sending a packet, require enough tokens for `32 + payload_len`;
- if not enough tokens are available, leave response state unchanged and return.

Also bound per-call work independently of the token bucket so `mia_video_service`
does not monopolize the main loop.

## Bring-Up Phases

### Phase 1: Data Structures Only

- Add constants, dirty maps, status/event helpers, and unit-testable packet
  structs.
- Add `mia_video_init()` that initializes control block fields and dirty maps.
- Add `VIDEO_ENABLE`, `VIDEO_FORCE_FULL_REFRESH`, and `VIDEO_SET_MODE`
  handlers.
- Build and confirm no behavior change when video is disabled.

### Phase 2: Dirty Marking

- Hook `index_write()` and `index_write_and_step()`.
- Add debug counters for dirty marks and dirty pages.
- Verify writes inside `$00000-$10D4F` set expected bits.
- Verify writes outside video state do not set bits.
- Verify final page and padding bits are handled correctly.

### Phase 3: Dirty Rotation and Page List

- Implement dirty-map empty check, full-refresh pending-map marking,
  core-1-safe active/pending rotation, ACK cleanup, and pending page list
  generation.
- Drive it from a temporary debug command before UDP exists.
- Verify:
  - clean request returns no dirty pages;
  - sparse dirty pages produce sorted `pending_pages`;
  - full refresh produces 2,155 pages;
  - new writes after rotation go to the active map;
  - ACK clears the old pending map before `VIDEO_EVENT_FRAME_ACKED`.

### Phase 4: Packet Builder Without Wi-Fi

- Implement packet construction into a caller-provided buffer.
- Test page records, chunk counts, final page padding, and validation helpers.
- Verify deterministic chunk mapping by generating the same chunk twice after
  intervening writes.

### Phase 5: UDP Session and Parameters

- Add UDP PCB, `HELLO`, `WELCOME`, `SET_PARAMS`, `PARAMS_ACCEPTED`, and
  `STATUS`.
- Add session endpoint tracking, timeout, and `STATUS_BUSY`.
- Add `cyw43_arch_poll()` and `mia_video_service()` to the main loop.
- Confirm one client can connect and a second endpoint gets busy status.

### Phase 6: Frame Responses

- Implement `REQUEST_FRAME` handling.
- Send `FRAME_DATA` chunks over bounded service calls.
- Implement `ACK_RESPONSE`.
- Update `VIDEO_STATUS`, `FRAME_ID`, and events through the lifecycle.
- Verify `NO_DIRTY_PAGES` does not assign a frame id or require ACK.

### Phase 7: Repair

- Implement `NACK_CHUNKS`.
- Regenerate requested chunks from `pending_pages`.
- Verify old/unknown repairs are ignored and impossible repairs force full
  refresh.
- Confirm repairs do not release pending state.

### Phase 8: Recovery and Edge Cases

- Implement implicit ACK via `REQUEST_FRAME(last_complete == pending.frame_id)`.
- Implement protocol-error full refresh.
- Implement session expiry cleanup.
- Test lost ACK, duplicate ACK, stale request, future ACK, reconnect, and full
  refresh after reconnect.

### Phase 9: Performance Pass

- Measure core 1 write-path overhead at intended PHI2 speeds.
- Measure core 0 dirty-map scan and packet builder time for sparse and full
  refresh cases.
- Tune `mia_video_service()` packet budget per call.
- Confirm normal gameplay-style dirty updates can sustain 25-30 request/ACK
  cycles per second when bandwidth permits.

## Test Plan

Host-side tests where possible:

- Header encode/decode and reserved-field validation.
- Parameter clamping.
- Frame id serial comparison.
- Dirty bitmap scanning.
- Page record packing.
- Chunk count math for payload sizes 256 and 512.
- Final-page padding.
- Repair chunk regeneration maps to the same page indexes.
- Request handling state machine.

Hardware/integration tests:

- 6502 writes to one video byte; client receives one page record.
- Repeated writes to same page produce one page record.
- Writes after dirty-map rotation are sent in the next response.
- Full refresh after connect sends 2,155 records.
- Lost chunk triggers repair.
- Lost ACK is handled by implicit ACK on next request.
- Duplicate/old ACK is ignored.
- Future ACK forces full refresh.
- Client disconnect expires session and next client receives full refresh.
- `VIDEO_READOUT_ACTIVE`, `VIDEO_RESPONSE_SENT`, and `VIDEO_UPDATE_ACTIVE`
  transitions match the documented lifecycle.
- Video IRQ fires only for enabled event bits and clears when event status bits
  are write-cleared.

## Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| Dirty mark slows core 1 bus path | keep helper inline/RAM-resident, no locks, no allocation, profile early |
| Write-clear video events add hot-path branch | profile option 1 vs command-based event clear |
| UDP send monopolizes core 0 | cap chunks per service call and use token bucket |
| Repair reads newer live values | documented readout-window behavior; programmers can wait for ACK |
| Full refresh is too slow | expected; use only on connect/recovery and keep normal dirty updates small |
| Wi-Fi stack setup exceeds current main loop assumptions | integrate incrementally after packet builder tests |
| Dirty map rotation races with core 1 writes | core 0 requests rotation; core 1 performs it between bus actions |

## Definition of Done

- MIA can accept one client session and negotiate parameters.
- First update after connection is a full refresh.
- Normal updates send only dirty pages in deterministic ascending page order.
- One outstanding response is enforced.
- Missing chunks can be regenerated without retaining a full response buffer.
- ACK, implicit ACK, stale ACK, future ACK, stale request, retry, and session
  expiry follow [video-protocol.md](video-protocol.md).
- Core 1 dirty marking remains bounded and measured at target PHI2 speeds.
- Status bits and video IRQ events match [video-output.md](video-output.md) and
  [video-programmer-guide.md](video-programmer-guide.md).
