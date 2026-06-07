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
and tracks syncable page changes with a 270-byte dirty map. Page 0 is local
MIA/6502 control state and is never sent to the client. Syncable render state
uses absolute page indexes `1-2154`. Core 1 sets the bit for the syncable page
it just wrote; core 0 later uses a captured dirty map to decide which pages to
send to the client.

## Design Constraints

- Core 1 owns the time-critical 6502 bus action loop.
- Core 1 must not build packets, scan dirty maps, allocate memory, poll Wi-Fi,
  or wait for core 0.
- A syncable video-memory write on core 1 should remain bounded: write one byte
  to MIA RAM and mark one dirty bit.
- Core 0 owns all slow video work: dirty-map rotation, page list generation,
  packet construction, UDP send/receive, repair, bounded send scheduling, status,
  and lifecycle events.
- Update responses read live MIA RAM. The pending dirty map freezes the list of
  pages for a response, not the byte values in those pages.
- Repair regenerates chunks from the retained pending dirty map using the same
  deterministic page order.
- Full refresh is implemented by marking every syncable video page dirty.

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
#define MIA_VIDEO_FIRST_SYNC_PAGE  1u
#define MIA_VIDEO_SYNC_PAGE_COUNT  (MIA_VIDEO_PAGE_COUNT - MIA_VIDEO_FIRST_SYNC_PAGE)
#define MIA_VIDEO_SYNC_START       32u
#define MIA_VIDEO_DIRTY_MAP_SIZE   270u
#define MIA_VIDEO_PAGE_RECORD_SIZE 34u
#define MIA_VIDEO_HEADER_SIZE      32u
#define MIA_VIDEO_DEFAULT_PAYLOAD  512u
```

The dirty map keeps absolute page positions, so bit 0 is reserved for the local
control page and must remain clear or be ignored by scans. The final syncable
page has only 16 valid bytes. Packet generation still sends 32 bytes for that
page; the client ignores the padding.

## Control Pages Integration

The first 256 bytes of video RAM are the video control area. Page 0
(`$00000-$0001F`) is local MIA/6502 control state. It is never dirty and is
never sent in `FRAME_DATA`. Add named offsets or a carefully packed access layer
for:

| Offset | Field |
| ---: | --- |
| `$00` | `VIDEO_VERSION` |
| `$04-$07` | `FRAME_ID` |
| `$08-$09` | `LAST_RESPONSE_DIRTY_PAGES` |

`LAST_RESPONSE_DIRTY_PAGES` is maintained by core 0 at stable response
boundaries. For a `FRAME_DATA` response, update it after the first complete send
has finished and before publishing `MIA_STAT_VIDEO_FRAME_SENT` and
`IRQ_VIDEO_FRAME_SENT`. It should not add work to the core 1 dirty-mark hot
path. A full refresh reports `2,154`.

Page 1 (`$00020-$0003F`) is syncable render control state:

| Offset | Field |
| ---: | --- |
| `$20` | `VIDEO_MODE` |
| `$21` | `LAYER_ENABLE` |
| `$22` | `BG_VIEWPORT_MODE` |
| `$23` | `BG_ACTIVE_SET` |
| `$24-$25` | `SCROLL_X` |
| `$26-$27` | `SCROLL_Y` |
| `$28-$2C` | bank selectors |
| `$2D` | `CHR_1BPP_MASK` |
| `$2E` | `CHR_1BPP_PLANES` |
| `$2F` | `BACKDROP_COLOR` |
| `$30` | `OAM_LAST_INDEX` |

Add video lifecycle bits to [src/mia/etc/status.h](../src/mia/etc/status.h).
These are level bits in the general `MIA_STATUS` register, not fields in video
RAM:

```c
#define MIA_STAT_VIDEO_FRAME_REQUESTED  (1u << 5)
#define MIA_STAT_VIDEO_FRAME_SENT       (1u << 6)
```

Add video event bits to [src/mia/irq/irq.h](../src/mia/irq/irq.h). These are
latched pending bits in the normal `IRQ_STATUS` register and are enabled through
the normal `IRQ_MASK` register:

```c
#define IRQ_VIDEO_FRAME_REQUEST  (1u << 5)
#define IRQ_VIDEO_FRAME_SENT     (1u << 6)
#define IRQ_VIDEO_FRAME_ACKED    (1u << 7)
```

Use `mia_irq_set_flag()` to publish these events. The existing IRQ evaluation
then updates `IRQ_TRIGGERED` and the physical IRQ line according to `IRQ_MASK`.

## Commands

Add video command handlers in the command table:

| Command | Id | Core 0 handler |
| --- | ---: | --- |
| `VIDEO_ENABLE` | `$40` | initialize video control state and video indexes |
| `VIDEO_FORCE_FULL_REFRESH` | `$42` | mark every syncable video page dirty for the next update |
| `VIDEO_SET_MODE` | `$43` | update `VIDEO_MODE` bits |

Suggested command behavior:

- `VIDEO_ENABLE` clears video state, initializes the video control pages,
  configures video index descriptors, clears both dirty maps, and marks
  every syncable video page dirty in the active map.
- `VIDEO_FORCE_FULL_REFRESH` marks every syncable video page dirty in the active
  map and leaves pending response state alone. The next accepted response is
  therefore a normal update containing the whole client mirror.
- `VIDEO_SET_MODE` updates the `VIDEO_MODE` field and can trigger full refresh
  if a mode change invalidates the client renderer's assumptions.

Do not implement `VIDEO_COMMIT_FRAME`; it belongs to the older protocol model.

## Video Index Setup

`VIDEO_ENABLE` should configure the video indexes listed in
[video-output.md](video-output.md):

| Index | Range | Length |
| ---: | ---: | ---: |
| `$70` | `$00000-$0001F` | 32 |
| `$71` | `$00004-$00007` | 4 |
| `$72` | `$00008-$00009` | 2 |
| `$73-$7F` | reserved local-control indexes | - |
| `$80` | `$00020-$0003F` | 32 |
| `$81` | `$00021-$00021` | 1 |
| `$82` | `$00022-$00023` | 2 |
| `$83` | `$00024-$00025` | 2 |
| `$84` | `$00026-$00027` | 2 |
| `$85` | `$00028-$0002C` | 5 |
| `$86` | `$0002D-$0002E` | 2 |
| `$87` | `$0002F-$0002F` | 1 |
| `$88` | `$00030-$00030` | 1 |
| `$89-$8F` | reserved render-control indexes | - |
| `$90-$9F` | palette banks, `$00100 + n * $10` | 16 each |
| `$A0-$A7` | CHR banks | 6,144 each |
| `$A8-$AF` | background nametables | 1,000 each |
| `$B0-$B7` | background attributes | 1,000 each |
| `$B8` | `$10080-$10467` | 1,000 |
| `$B9` | `$10468-$1084F` | 1,000 |
| `$BA-$BF` | reserved video indexes | - |
| `$C0-$DF` | OAM sprite records, `$10850 + n * 5` | 5 each |
| `$E0-$FF` | reserved video indexes | - |

All video indexes should use forward step-on-read, forward step-on-write, and
wrap. Video lifecycle status is read through the normal `MIA_STATUS` register,
so no video status index is needed.

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
    if (addr < MIA_VIDEO_SYNC_START || addr >= MIA_VIDEO_STATE_SIZE) {
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

Avoid touching dirty maps for writes to page 0 or outside the video state range.
This keeps MIA-internal lifecycle/status writes, general MIA RAM, and index
descriptor traffic from creating video updates.

## Video Lifecycle Events

The dirty maps connect two independent event streams:

- core 1 receives 6502 bus writes and marks syncable video pages dirty;
- core 0 receives UDP packets from the client and turns dirty pages into update
  responses.

The lifecycle is:

| Event | Meaning | Dirty-map action | 6502-visible state |
| --- | --- | --- | --- |
| 6502 writes syncable video memory | one byte in live MIA RAM changed | set one bit in the active dirty map | no status event; the write is only queued for a future update |
| client connects or recovery is required | client mirror must be rebuilt | mark every syncable video page dirty in the active map | next update is a full refresh |
| client sends `REQUEST_FRAME` and no response is pending | client asks MIA to publish accumulated changes | rotate active dirty map to pending; already-clear other map becomes active | set `MIA_STAT_VIDEO_FRAME_REQUESTED` and `IRQ_VIDEO_FRAME_REQUEST` |
| first response send completes | every dirty page chunk was sent once | keep pending map for possible repair | set `MIA_STAT_VIDEO_FRAME_SENT` and `IRQ_VIDEO_FRAME_SENT` |
| client sends `NACK_CHUNKS` | client missed one or more chunks | regenerate those chunks from the pending map | no public lifecycle status or IRQ event change |
| client sends `ACK_RESPONSE` | client applied the complete update | clear pending map, release pending state, and advance acknowledged frame id | clear `MIA_STATUS` video lifecycle bits and set `IRQ_VIDEO_FRAME_ACKED` after cleanup |

This event model defines the write timing rules. Writes before an accepted
`REQUEST_FRAME` are named by the pending map and are intended for that update.
Writes after the rotation go into the new active map for the next update. Because
MIA reads live RAM, visible writes while `MIA_STAT_VIDEO_FRAME_REQUESTED` is set
can still be read into the current response. Visible writes after
`MIA_STAT_VIDEO_FRAME_SENT` is set but before `ACK_RESPONSE` can appear in
repair chunks for the same response. A program that wants strict visual
cleanliness waits until `IRQ_VIDEO_FRAME_ACKED` or until both `MIA_STATUS`
video lifecycle bits are clear before writing visible video memory.

## Core 0 Dirty-Map Rotation

When a valid `REQUEST_FRAME` arrives and no response is pending:

1. If the active dirty map has no syncable page bits set, send
   `STATUS(NO_DIRTY_PAGES)` and do not assign a new frame id.
2. Derive the available map as `active_idx ^ 1`. It must already be clear.
3. Request a core-1-safe rotation and wait for `video_rotate_done`.
4. Set `video_pending_dirty_index` to the old active index and
   `video_pending_valid = true`.
5. Scan the pending dirty map into a page index list.
6. Assign the next nonzero `FRAME_ID`.
7. Set `MIA_STAT_VIDEO_FRAME_REQUESTED`.
8. Set `IRQ_VIDEO_FRAME_REQUEST`.

Write `0` to `LAST_RESPONSE_DIRTY_PAGES` when a request returns
`STATUS(NO_DIRTY_PAGES)`. For a dirty response, do not update
`LAST_RESPONSE_DIRTY_PAGES` at request-accept time; it must keep the previous
stable value until the first complete send finishes. Update `FRAME_ID` before
setting `IRQ_VIDEO_FRAME_REQUEST`, so a 6502 IRQ handler can inspect the new
frame id immediately.

The pending dirty map must not be modified until the pending response is
acknowledged, implicitly acknowledged, reset by `HELLO`, or discarded during
protocol-error recovery.

For fast packet generation and repair, build:

```c
static uint16_t pending_pages[MIA_VIDEO_SYNC_PAGE_COUNT];
static uint16_t pending_page_count;
```

Worst case list size is 4,308 bytes. This avoids repeated bitmap scans during
send and repair.

## Dirty-Map Scan

Scan the 270-byte map using byte skips and count-trailing-zero logic:

```c
for (uint32_t byte_i = 0; byte_i < MIA_VIDEO_DIRTY_MAP_SIZE; ++byte_i) {
    uint8_t bits = pending_dirty[byte_i];
    if (byte_i == 0) {
        bits &= 0xfeu;
    }
    while (bits != 0) {
        uint32_t bit = __builtin_ctz(bits);
        uint32_t page = byte_i * 8u + bit;
        if (page >= MIA_VIDEO_FIRST_SYNC_PAGE && page < MIA_VIDEO_PAGE_COUNT) {
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
- Dispatch `HELLO`, `REQUEST_FRAME`, `ACK_RESPONSE`, `NACK_CHUNKS`, and optional
  client `STATUS`.

Plan UDP send with bounded per-service work:

- Send at most a small number of chunks per `mia_video_service()` call.
- Respect pbuf allocation failure by retrying later.
- Avoid blocking on Wi-Fi or pbuf availability.

## Session State

Use one active client endpoint:

```c
typedef struct {
    bool active;
    ip_addr_t addr;
    uint16_t port;
    uint32_t session_id;
    uint32_t client_frame_id;
    uint32_t latest_frame_id;
} mia_video_session_t;
```

Session rules:

- Any valid `HELLO` resets the video session and returns `WELCOME`.
- Reset discards pending response state, clears pending/update status and client
  frame tracking, assigns a fresh nonzero `session_id`, records the sender
  endpoint, and marks every syncable video page dirty.
- Session id `0` is only valid for `HELLO` and no-session status.
- Packets with stale or unknown nonzero session ids are ignored.

Generate nonzero `session_id` from available entropy or a mixed timer/counter
fallback. Avoid assigning `0`. This is a session-generation token, not a client
index; 32 bits make accidental collision with stale packets from a recently reset
session negligible.

## Packet Size Constants

Version 1 uses fixed packet sizing:

```c
#define MIA_VIDEO_MAX_UDP_PAYLOAD   512u
#define MIA_VIDEO_RECORDS_PER_CHUNK 14u
#define MIA_VIDEO_CHUNK_PAYLOAD     476u
```

Client request cadence, retry count, and repair/session-loss timeouts are
client-local policy; MIA does not store or enforce those values.

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
- Clear `MIA_STAT_VIDEO_FRAME_REQUESTED` and `MIA_STAT_VIDEO_FRAME_SENT`.
- Set `IRQ_VIDEO_FRAME_ACKED`.

The pending dirty map must be cleared before the `MIA_STATUS` video lifecycle
bits are cleared and before `IRQ_VIDEO_FRAME_ACKED` is set. After the event is
visible to the 6502, the
non-active map is already clear and ready for the next `REQUEST_FRAME` rotation.

Duplicate/old/unknown ACKs are ignored. Future/impossible ACKs are protocol
errors and invalidate the current session.

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

1. Compute `first_record = chunk_index * MIA_VIDEO_RECORDS_PER_CHUNK`.
2. Compute `record_count = min(MIA_VIDEO_RECORDS_PER_CHUNK, page_count - first_record)`.
3. For each record:
   - page index = `pending_pages[first_record + i]`;
   - offset = `page_index * 32`;
   - write little-endian `page_index`;
   - copy 32 bytes from `mem[offset]`, padding the final page beyond valid
     video state with zero.
4. Send UDP packet with `payload_len = record_count * 34`.

Do not include page 0 or clean pages. Do not merge ranges or use data-dependent
fill compression in v1; deterministic fixed-size page records make repair
simple.

## Request Handling

`REQUEST_FRAME` cases:

| State | Request | MIA action |
| --- | --- | --- |
| no pending | `last_complete == client_frame_id` | accept request or return `NO_DIRTY_PAGES` |
| no pending | `last_complete < client_frame_id` | ignore stale request; client timeout/`HELLO` handles true desync |
| no pending | `last_complete > client_frame_id` | protocol error, invalidate session |
| pending | same `request_id` and same base | resend/regenerate pending response |
| pending | `last_complete == pending.frame_id` | implicit ACK, then handle request |
| pending | `last_complete > pending.frame_id` | protocol error, invalidate session |
| pending | other stale/duplicate/early request | ignore request |

`STATUS(NO_DIRTY_PAGES)` does not assign a frame id and does not require ACK.

When accepting a request with syncable dirty pages:

- increment nonzero `FRAME_ID`;
- update the local control page `FRAME_ID`;
- create the pending response;
- send chunks over successive service calls.

When the first complete send for that response finishes:

- update `LAST_RESPONSE_DIRTY_PAGES` to the sent dirty page count;
- set `MIA_STAT_VIDEO_FRAME_SENT`;
- set `IRQ_VIDEO_FRAME_SENT`.

## Repair Handling

`NACK_CHUNKS` cases:

| Packet | MIA action |
| --- | --- |
| matches pending response and indexes valid | regenerate/send requested chunks |
| old/duplicate/unknown response | ignore |
| future/impossible response | protocol error, invalidate session |
| malformed payload/index list | protocol error, invalidate session |

Repair lifecycle:

1. Send requested chunks using the pending page list and current RAM values.
2. Leave the `MIA_STATUS` video lifecycle bits and `IRQ_STATUS` video event bits
   unchanged.

Repair does not release pending response state. Only ACK, implicit ACK, a new
`HELLO` session reset, or protocol-error recovery releases it.

## Full Refresh Handling

Implement:

```c
void mia_video_force_full_refresh(void);
```

It should:

- safely mark every syncable video page dirty in the active dirty map;
- leave pending response state alone.

Core 1 can write the active dirty map at any time, so core 0 must not fill that
map concurrently with the hot write path. Use a core-1-safe coordination point:
core 0 requests a mark-all operation, core 1 applies it between bus actions, and
core 0 waits for completion before treating every syncable video page as dirty.

For protocol-error recovery:

1. send `STATUS(PROTOCOL_ERROR)` if useful and safe;
2. discard pending response state if any;
3. clear the old pending dirty map before releasing it;
4. set `video_pending_valid = false`;
5. clear the `MIA_STATUS` video lifecycle bits;
6. invalidate the current session id/endpoint so only `HELLO` can restart video.

The next valid `HELLO` performs the normal session reset, marks every syncable
video page dirty, and returns `WELCOME`.

## Status and IRQ Helpers

Create helpers instead of open-coding video lifecycle bit twiddling:

```c
void mia_video_status_set(uint8_t bits);
void mia_video_status_clear(uint8_t bits);
void mia_video_irq_set(uint16_t flag);
```

The status helpers set and clear `MIA_STAT_VIDEO_FRAME_REQUESTED` and
`MIA_STAT_VIDEO_FRAME_SENT` in the general `MIA_STATUS` register. The IRQ helper
can be a thin wrapper around `mia_irq_set_flag()` for
`IRQ_VIDEO_FRAME_REQUEST`, `IRQ_VIDEO_FRAME_SENT`, and
`IRQ_VIDEO_FRAME_ACKED`.

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

## Send Budget

Bound per-call work so `mia_video_service()` does not monopolize the main loop:

- send at most a small number of `FRAME_DATA` chunks per service call;
- stop immediately if pbuf allocation or UDP send fails and retry on a later
  service call;
- leave response state unchanged when a send is deferred;
- rely on lwIP/Wi-Fi backpressure rather than a protocol rate limit.

## Bring-Up Phases

### Phase 1: Data Structures Only

- Add constants, dirty maps, status/IRQ helpers, and unit-testable packet
  structs.
- Add `mia_video_init()` that initializes control page fields and dirty maps.
- Add `VIDEO_ENABLE`, `VIDEO_FORCE_FULL_REFRESH`, and `VIDEO_SET_MODE`
  handlers.
- Build and confirm no behavior change when video is disabled.

### Phase 2: Dirty Marking

- Hook `index_write()` and `index_write_and_step()`.
- Add debug counters for dirty marks and dirty pages.
- Verify writes inside `$00020-$10D4F` set expected bits.
- Verify writes inside `$00000-$0001F` do not set dirty bits.
- Verify writes outside video state do not set bits.
- Verify final page and padding bits are handled correctly.

### Phase 3: Dirty Rotation and Page List

- Implement dirty-map empty check, core-1-safe full-refresh marking,
  active/pending rotation, ACK cleanup, and pending page list generation.
- Drive it from a temporary debug command before UDP exists.
- Verify:
  - clean request returns no dirty pages;
  - sparse dirty pages produce sorted `pending_pages`;
  - full refresh produces 2,154 pages;
  - new writes after rotation go to the active map;
  - ACK clears the old pending map before `IRQ_VIDEO_FRAME_ACKED`.

### Phase 4: Packet Builder Without Wi-Fi

- Implement packet construction into a caller-provided buffer.
- Test page records, chunk counts, final page padding, and validation helpers.
- Verify deterministic chunk mapping by generating the same chunk twice after
  intervening writes.

### Phase 5: UDP Session

- Add UDP PCB, `HELLO`, `WELCOME`, and `STATUS`.
- Add session endpoint tracking.
- Add `cyw43_arch_poll()` and `mia_video_service()` to the main loop.
- Confirm any valid `HELLO` resets the session, assigns a fresh session id, and
  marks every syncable video page dirty.

### Phase 6: Frame Responses

- Implement `REQUEST_FRAME` handling.
- Send `FRAME_DATA` chunks over bounded service calls.
- Implement `ACK_RESPONSE`.
- Update `MIA_STATUS`, `FRAME_ID`, and video events through the lifecycle.
- Verify `NO_DIRTY_PAGES` does not assign a frame id or require ACK.
- Verify `LAST_RESPONSE_DIRTY_PAGES` reports the sent dirty page count at
  `IRQ_VIDEO_FRAME_SENT`, and reports `0` for `NO_DIRTY_PAGES`.

### Phase 7: Repair

- Implement `NACK_CHUNKS`.
- Regenerate requested chunks from `pending_pages`.
- Verify old/unknown repairs are ignored and impossible repairs invalidate the
  session; the following `HELLO` recovery produces a full refresh.
- Confirm repairs do not release pending state.

### Phase 8: Recovery and Edge Cases

- Implement implicit ACK via `REQUEST_FRAME(last_complete == pending.frame_id)`.
- Implement protocol-error session invalidation.
- Implement `HELLO` session-reset cleanup.
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
- Frame id unsigned comparison.
- Dirty bitmap scanning.
- `LAST_RESPONSE_DIRTY_PAGES` updates for sparse, full-refresh, and no-dirty
  results.
- Page record packing.
- Chunk count math with fixed 14-record chunks.
- Final-page padding.
- Repair chunk regeneration maps to the same page indexes.
- Request handling state machine.

Hardware/integration tests:

- 6502 writes to one video byte; client receives one page record.
- Repeated writes to same page produce one page record.
- Writes after dirty-map rotation are sent in the next response.
- 6502 writes to page 0 do not create client page records.
- Full refresh after connect sends 2,154 records.
- Lost chunk triggers repair.
- Lost ACK is handled by implicit ACK on next request.
- Duplicate/old ACK is ignored.
- Future ACK invalidates the session; the following `HELLO` receives full refresh.
- New `HELLO` resets the session and the client receives full refresh.
- `MIA_STAT_VIDEO_FRAME_REQUESTED` and `MIA_STAT_VIDEO_FRAME_SENT` transitions
  match the documented lifecycle.
- Video IRQ fires only for enabled event bits and clears through the normal
  `IRQ_STATUS`/`IRQ_MASK` behavior.

## Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| Dirty mark slows core 1 bus path | keep helper inline/RAM-resident, no locks, no allocation, profile early |
| UDP send monopolizes core 0 | cap chunks per service call and yield on pbuf/UDP backpressure |
| Repair reads newer live values | documented live-read behavior; programmers can wait for ACK |
| Full refresh is too slow | expected; use only on connect/recovery and keep normal dirty updates small |
| Wi-Fi stack setup exceeds current main loop assumptions | integrate incrementally after packet builder tests |
| Dirty map rotation races with core 1 writes | core 0 requests rotation; core 1 performs it between bus actions |

## Definition of Done

- MIA can accept one client session and reset it on any valid `HELLO`.
- First update after connection is a full refresh.
- Normal updates send only dirty pages in deterministic ascending page order.
- One outstanding response is enforced.
- Missing chunks can be regenerated without retaining a full response buffer.
- ACK, implicit ACK, stale ACK, future ACK, stale request, retry, and reconnect
  follow [video-protocol.md](video-protocol.md).
- Core 1 dirty marking remains bounded and measured at target PHI2 speeds.
- Status bits and video IRQ events match [video-output.md](video-output.md) and
  [video-programmer-guide.md](video-programmer-guide.md).
