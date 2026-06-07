# MIA Video Protocol

## Introduction

MIA is the Multifunction Interface Adapter for the Clementina 6502 computer,
implemented on a Raspberry Pi Pico 2 W. It appears to the 6502 as a 32-byte
register block at `$FFE0-$FFFF`, generates the 6502 clock, handles the data
bus, and provides 128 KiB of internal RAM through indexed memory windows. The
video service uses that adapter role to give Clementina Wi-Fi video output
without adding a framebuffer or video encoder to the 6502 side.

At a high level, MIA is the 6502-facing transmitter and state owner for a
remote video system. A 6502 program writes graphics state into MIA RAM: render
control registers, palettes, character graphics, background and overlay
nametables, attributes, and sprite records. A host client keeps a complete
mirror of the syncable render state and renders the final 320x200 image
locally. MIA does not stream raw pixels and does not run a video codec.

The video state occupies the first 68,944 bytes of MIA's 128 KiB RAM. That
region starts with a 32-byte local control page that is visible to the 6502 but
is never sent to the client. Syncable render state begins at offset `$00020`
with the render control page, followed by reserved syncable control space,
palette data, CHR tile graphics, background nametables and attributes, overlay
tables, and OAM sprite records. The full layout is defined in
[video-output.md](video-output.md); this protocol treats offsets `$00020`
through `$10D4F` as the state mirrored by the client.

For update tracking, MIA divides that 68,944-byte video region into 32-byte
absolute pages. There are 2,155 addressable video pages, so one dirty map is
2,155 bits, or 270 bytes. Page 0 is local control state; bit 0 is reserved and
ignored. The 2,154 syncable pages use absolute page indexes `1-2154`. When the
6502 writes inside the syncable render state, MIA writes the byte to live RAM
and sets the matching dirty bit. Rewriting another byte in the same page only
sets the same bit again. Writes to page 0 do not create dirty video pages.

MIA maintains two dirty maps in parallel. One is the `active` that keeps track
of the updates the 6502 is doing. The other is `pending` which is the one being
used to generate the video frame sent to the client.

The protocol is client-paced. The client requests video updates at the rate it
can display and receive them.

The 6502 can change any byte in VRAM. MIA does not send anything at write time;
for syncable video memory, it records that the corresponding 32-byte page "is
dirty" in the `active` map and must eventually be published to the client.

When the client is ready for another video update, it sends `REQUEST_FRAME`. If
no response is already pending, MIA accepts the request and rotates the
dirty-map roles: the active map becomes the pending map for this response, and
the other map becomes the new active map for later 6502 writes. Moving the
`active` map to the pending role captures the list of dirty pages that must be
sent on the next frame and allows the 6502 to keep marking dirty pages that
will be sent in a later frame request in the `active` map.

MIA then reads the pages named by the pending map and sends them as
`FRAME_DATA`. Since the protocol uses UDP and a frame update may be larger than
one datagram, the response is split into numbered chunks. MIA sends every chunk
once, but it cannot release the pending map: UDP packets may be lost, and the
client may request the missing chunks.

If chunks are missing, the client sends `NACK_CHUNKS`. MIA uses the retained
pending map to regenerate the requested chunks in the same deterministic page
order. Only after the client has received and applied the complete update does
it send `ACK_RESPONSE`. MIA handles that acknowledgement by clearing the pending
map first (this is needed so when it becomes active doesn't have any pages
marked as dirty), then publishing the acknowledgement through `MIA_STATUS` and
`IRQ_STATUS`. After this cleanup the pending map is ready to become active on
the next accepted `REQUEST_FRAME`.

So the 6502 programmer will observe the following lifecycle in a typical frame
to frame update:

```text
all clear -> FRAME_REQUEST -> FRAME_SENT -> FRAME_ACKED -> all clear
```

The programmer can change the VRAM at any point in this lifecycle but the
changes may have consequences if they are done in the visible parts of the
memory (being drawn on the screen).

- **All Clear**: The last frame has been acknowledged (client has applied the
update to its local copy of memory). No new frame has been requested yet.
Programmer can change any VRAM memory with no visual inconsistencies until the
next FRAME_REQUEST arrives.
- **FRAME_REQUEST**: Client has requested a frame. Frame is being prepared for
transmission. The programmer should change only non-visible memory. MIA is
packing the frame for transmission. Any change made on visible memory that is
marked as dirty on the frame being packed may or may not be packed and
transmitted causing visual inconsistencies. Updates to non-visible VRAM memory
are fine.
- **FRAME_SENT**: All the chunks for the requested frame have been sent at least
once. The programmer should change only non-visible memory. Since the client has
not acknowledged yet, if the programmer changes a page that is marked as dirty
and needs to be sent again due to issues in the network, the changes may be
included in a repair chunk causing visual inconsistencies.
- **FRAME_ACKED**: The client has acknowledged the last frame (has received it
in full and updated its local VRAM copy). After this event the protocol is
back to "all clear", the programmer can make changes to any register without
risking visual inconsistencies until the next FRAME_REQUEST.

MIA exposes the request, sent, and acknowledged transitions as IRQ-capable
events. This allows the programmer to tie the game loop to any of them depending
on convenience. For example, stepping a game loop only after ACK will ensure
that every frame is sent to the client at the cost of the whole application
running slower if there are issues in the network. On the other side, the
programmer may choose to step the game loop independently and only build the
frame when ACK allows it. This lets the program keep running independently from
network updates or repair delays, but if the network falls behind the displayed
video can lag or feel jumpy.

The protocol serves one active client at a time. On connection and on
unrecoverable mismatch, MIA marks every syncable video page dirty. The next
accepted update rebuilds the client mirror; no separate snapshot packet type is
required.

## Transport

Video uses UDP over IPv4.

UDP keeps latency and firmware complexity low. Packet loss is handled in the
application protocol through chunking, complete-response acknowledgements,
missing-chunk repair, deterministic dirty-page packing, and full-refresh
recovery. Payload integrity is provided by the link layer and UDP checksum;
corrupted datagrams are treated as missing packets.

The protocol is intended for trusted local networks and supports one active
client session at a time.

## Defaults and Limits

Version 1 has fixed wire sizes. Client-local policy is not configured in MIA; the
client simply uses it to decide when to send protocol messages.

Fixed wire constants:

| Constant | Value | Notes |
| --- | ---: | --- |
| Protocol header | 32 B | every packet starts with this header |
| Dirty page size | 32 B | last page is padded on the wire |
| Addressable video page count | 2,155 | `ceil(68,944 / 32)` |
| Syncable video page count | 2,154 | absolute page indexes `1-2154`; page 0 is local only |
| Dirty map size | 270 B | `ceil(2,155 / 8)`; bit 0 is reserved |
| Dirty map count | 2 | one active set and one pending response set |
| Page record size | 34 B | 2-byte page index plus 32 data bytes |
| Application UDP payload | 512 B | includes the 32-byte protocol header |
| Response bytes per packet | 476 B | 14 page records at the fixed UDP payload size |
| Full refresh payload | 73,236 B | all 2,154 syncable page records |
| Full refresh chunks | 154 | at the fixed UDP payload size |

Client-local policy:

| Policy | Default | Recommended Range | Notes |
| --- | ---: | ---: | --- |
| Request FPS | 25 | 5-30 | client `REQUEST_FRAME` cadence; MIA does not send frames on its own |
| Repair timeout | 100 ms | 30-500 ms | client delay before `NACK_CHUNKS` |

Version 1 has exactly one outstanding update response. The client does not ask
for another update while a response is incomplete, except for the explicit retry
and lost-ACK cases defined below.

## Endianness

All multi-byte fields are little-endian.

## Packet Header

Every packet starts with a 32-byte header:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `magic` | `0x4D56` (`"MV"` little-endian); rejects packets for other protocols |
| 2 | 1 | `version` | protocol version, `1` |
| 3 | 1 | `type` | packet type; see [Packet Types](#packet-types) |
| 4 | 4 | `session_id` | session token assigned by MIA in `WELCOME`; `0` is used before a session exists |
| 8 | 4 | `seq` | endpoint-local packet sequence number for diagnostics/liveness |
| 12 | 4 | `ack` | newest peer `seq` observed by this endpoint, or `0` if none |
| 16 | 4 | `frame_id` | MIA-assigned update id for a `FRAME_DATA` response, or `0` for non-frame packets |
| 20 | 2 | `request_id` | client-assigned id for a `REQUEST_FRAME`; echoed by MIA in the matching response |
| 22 | 2 | `chunk_index` | zero-based chunk number for `FRAME_DATA`; otherwise `0` |
| 24 | 2 | `chunk_count` | total chunks in this `FRAME_DATA` response; otherwise `0` |
| 26 | 2 | `payload_len` | bytes after this header |
| 28 | 2 | `flags` | reserved for future type-specific flags; sender writes zero |
| 30 | 2 | `reserved` | sender writes zero; receiver validates zero |

### Header Field Semantics

`session_id` answers "which client session does this packet belong to?" The client
sends `HELLO` with `session_id = 0`. MIA replies with `WELCOME` and a fresh
nonzero `session_id`. After that, packets for the active session carry that
value. Packets from older sessions are ignored.

`seq` and `ack` are diagnostic fields. `seq` identifies this packet in the
sender's packet stream. `ack` reports the newest peer `seq` this endpoint has
observed, or `0` if it has not seen any peer packet yet. Together they help logs
confirm that packets are flowing in both directions. They do not drive
retransmission, frame ordering, chunk repair, or client mirror application.

`frame_id` answers "which MIA update response is this?" MIA assigns a new
nonzero `frame_id` only when it accepts a `REQUEST_FRAME` that produces
`FRAME_DATA`. `frame_id = 0` means no update has been produced or applied yet in
this session. A frame id identifies one dirty-page response; it does not mean all
bytes were sampled from MIA RAM at one instant.

The client tracks the newest complete `frame_id` it has applied and sends that
value as `last_complete_frame_id` in the next `REQUEST_FRAME`. MIA uses that
value to distinguish a normal request, a stale request, an implicit
acknowledgement after a lost `ACK_RESPONSE`, and an impossible future request.

`request_id` answers "which client request is this?" The client chooses a new
16-bit `request_id` for each new `REQUEST_FRAME`. MIA echoes that id in every
`FRAME_DATA`, `STATUS`, or repair response related to that request. If the client
received zero chunks and retries the same request, it reuses the same
`request_id`. The client must not reuse a `request_id` while the previous
response with that id is pending or repairable.

`chunk_index` and `chunk_count` are meaningful only for `FRAME_DATA`. They split
one response, identified by `request_id` and `frame_id`, into numbered UDP
datagrams. `NACK_CHUNKS` refers to these chunk indexes, not to dirty page
indexes.

`payload_len` is the number of bytes after the 32-byte header. Receivers validate
it against the packet type before reading payload fields.

All fields not meaningful for a packet type are written as zero by the sender.
Reserved fields are validated as zero; non-reserved unused fields are ignored by
the receiver.

## Packet Types

| Type | Name | Direction | Purpose |
| ---: | --- | --- | --- |
| `0x01` | `HELLO` | client to MIA | start a session |
| `0x02` | `WELCOME` | MIA to client | accept client and assign nonzero `session_id` |
| `0x05` | `REQUEST_FRAME` | client to MIA | request the next dirty-page update |
| `0x06` | `ACK_RESPONSE` | client to MIA | acknowledge a complete update response |
| `0x07` | `NACK_CHUNKS` | client to MIA | request missing chunks for the pending response |
| `0x20` | `FRAME_DATA` | MIA to client | chunk of a dirty-page update response |
| `0x30` | `STATUS` | either | empty update or protocol error |

Packet type values not listed above are reserved in version 1.

## Session Flow

Startup:

```text
client -> MIA: HELLO
MIA    -> client: WELCOME(session_id)
MIA marks every syncable video page dirty
client -> MIA: REQUEST_FRAME(last_complete_frame_id = 0)
MIA    -> client: FRAME_DATA chunks for full refresh
client -> MIA: ACK_RESPONSE(frame_id)
```

Steady state:

```text
client -> MIA: REQUEST_FRAME(last_complete_frame_id)
MIA    -> client: FRAME_DATA chunks, or STATUS(NO_DIRTY_PAGES)
client -> MIA: ACK_RESPONSE(frame_id) after applying a complete response
```

`STATUS(NO_DIRTY_PAGES)` does not create a pending response, does not assign a
new `frame_id`, and does not require `ACK_RESPONSE`.

The client sends at most one `REQUEST_FRAME` for a new update at a time. A
response is pending from the moment MIA accepts `REQUEST_FRAME` and assigns a
new `frame_id` until the client acknowledges that `frame_id`, MIA treats a later
request as an implicit acknowledgement, or a new `HELLO` resets the session.

If the client receives no chunks for a pending request before its repair timeout,
it retransmits the same `REQUEST_FRAME` with the same `request_id` and
`last_complete_frame_id`. MIA regenerates or resends the pending response for
that request.

If repeated retries for the same request receive no `FRAME_DATA` and no
`STATUS`, the client treats the video session as lost and sends a new `HELLO`.
This is the recovery path when the client has fallen behind MIA's
`client_frame_id` after MIA has already released older response state.

If the client applied the pending response but its `ACK_RESPONSE` was lost, the
next `REQUEST_FRAME` carries `last_complete_frame_id` equal to the pending
response's `frame_id`. MIA treats that request as an implicit acknowledgement of
the pending response, clears and releases the pending dirty set, and then
handles the new request normally.

Version 1 does not support session resume. Any valid `HELLO` resets the video
session: MIA discards pending response state if any exists, assigns a fresh
nonzero `session_id`, marks every syncable video page dirty, and returns
`WELCOME`.
Stale packets from an older session id are ignored.

## Client-To-MIA Packets

The client does not send video memory, pixels, CHR data, nametables, palettes,
or sprite state to MIA. MIA is the only source of video state.

Client-to-MIA packets are control and reliability messages:

| Packet | Payload |
| --- | --- |
| `HELLO` | none |
| `REQUEST_FRAME` | `last_complete_frame_id` |
| `ACK_RESPONSE` | none |
| `NACK_CHUNKS` | missing chunk indexes |
| `STATUS` | protocol error report |

### HELLO

`HELLO` starts or resets the active video session. It has no payload. The client
sends it with `session_id = 0`. Any valid `HELLO` causes MIA to discard pending
response state, assign a fresh nonzero `session_id`, mark every syncable video
page dirty, and reply with `WELCOME`.

### REQUEST_FRAME

`REQUEST_FRAME` asks MIA to publish the next dirty-page update. It carries a
client-generated `request_id` in the header and `last_complete_frame_id` in the
payload. The header `frame_id`, `chunk_index`, and `chunk_count` fields are `0`.

Payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 4 | `last_complete_frame_id` | newest complete update the client has applied, or `0` before the first full refresh |

If the client received zero chunks for a request, it retries the same
`REQUEST_FRAME` with the same `request_id` and `last_complete_frame_id`.

### ACK_RESPONSE

`ACK_RESPONSE` acknowledges that the client has received, validated, and applied
a complete `FRAME_DATA` response. It has no payload. The acknowledged response is
identified by the header `request_id` and `frame_id`.

### NACK_CHUNKS

`NACK_CHUNKS` asks MIA to resend missing chunks for the current pending response.
The pending response is identified by the header `request_id` and `frame_id`.
Missing indexes are chunk indexes for that response, not page record indexes.

Payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `missing_count` | number of chunk indexes that follow |
| 2 | 2 | `reserved` | sender writes zero; receiver validates zero |
| 4 | 2 * N | `missing_indexes` | zero-based missing chunk indexes for the pending response |

## MIA-To-Client Packets

MIA-to-client packets accept sessions, publish video state, and report protocol
state. The client never receives raw pixels; it receives dirty video-memory pages
and updates its local mirror.

| Packet | Payload |
| --- | --- |
| `WELCOME` | none |
| `FRAME_DATA` | dirty page records |
| `STATUS` | empty update or protocol error report |

### WELCOME

`WELCOME` accepts a client session. It has no payload. The assigned session token
is carried in the header `session_id` field.

After `WELCOME`, every syncable video page is dirty. The next accepted
`REQUEST_FRAME` produces a normal `FRAME_DATA` response whose pending dirty map
contains every syncable video page.

### FRAME_DATA

`FRAME_DATA` carries one chunk of a dirty-page response. All chunks in a response
carry the same `session_id`, `request_id`, `frame_id`, and `chunk_count`.
`chunk_index` identifies this packet's zero-based chunk position in the response.

A response contains one page record for each page whose bit was set in the
pending dirty map when MIA accepted the matching `REQUEST_FRAME`.

Each page record is fixed-size:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `page_index` | syncable video page index, `1-2154`; page 0 is never sent |
| 2 | 32 | `page_data` | bytes read from MIA RAM for that page |

The video address for a page record is:

```text
page_offset = page_index * 32
valid_len   = min(32, 68,944 - page_offset)
```

The final video page has only 16 valid bytes. Its remaining 16 wire bytes are
padding and are ignored by the client.

MIA emits page records in ascending `page_index` order. Clean pages are skipped.
This deterministic order is part of the repair protocol.

The number of records per chunk is fixed:

```text
records_per_chunk = floor((512 - 32) / 34) = 14
```

At the fixed 512-byte UDP payload:

```text
payload_len = 14 * 34 = 476 bytes, except the final chunk
```

`chunk_count = ceil(dirty_page_count / records_per_chunk)`. A `FRAME_DATA`
packet's payload contains the page records with indexes:

```text
first_record = chunk_index * records_per_chunk
last_record  = min(first_record + records_per_chunk, dirty_page_count) - 1
```

The client validates that page indexes are strictly increasing across the
complete response, within the syncable range, and present only once. Page index
0 is malformed. The client applies a response only after it has received every
chunk for that response. It then sends `ACK_RESPONSE`.

### STATUS

`STATUS` carries protocol outcomes that are not represented by `FRAME_DATA`.
MIA sends it for empty updates and protocol errors. A client may also send
`STATUS(PROTOCOL_ERROR)` to report that it rejected a malformed MIA response.

`STATUS(NO_DIRTY_PAGES)` does not create a pending response, does not assign a
new `frame_id`, and does not require `ACK_RESPONSE`.

`STATUS(PROTOCOL_ERROR)` invalidates the current video session. A client that
sends or receives `STATUS(PROTOCOL_ERROR)` restarts with `HELLO`; same-session
recovery after protocol error is not part of version 1.

`STATUS` packets carry this payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `status_code` | status reason |

`payload_len` is `2` for every `STATUS` packet.

Status codes:

| Code | Name | Meaning |
| ---: | --- | --- |
| `0` | `NO_DIRTY_PAGES` | request accepted but no syncable video pages were dirty |
| `1` | `PROTOCOL_ERROR` | malformed packet or impossible state |

## Update Semantics

Updates are driven by events from both sides of MIA. The 6502 side changes video
memory; the client side asks MIA to publish those changes. Dirty maps are the
bridge between those two timelines: a write records that a 32-byte page changed,
and a client request turns the accumulated dirty pages into a response.

MIA exposes this lifecycle to the 6502 through two level bits in the general
`MIA_STATUS` register and three IRQ-capable events in the local video control
page. When `MIA_STAT_VIDEO_FRAME_REQUESTED` and `MIA_STAT_VIDEO_FRAME_SENT` are
both clear, no update is currently retained for send, repair, or
acknowledgement. `MIA_STAT_VIDEO_FRAME_REQUESTED` is set after MIA accepts a
`REQUEST_FRAME` and remains set until the matching ACK.
`MIA_STAT_VIDEO_FRAME_SENT` is set after the initial send finishes and also
remains set until ACK. `IRQ_VIDEO_FRAME_REQUEST`, `IRQ_VIDEO_FRAME_SENT`, and
`IRQ_VIDEO_FRAME_ACKED` are latched event bits in the normal `IRQ_STATUS`
register. The existing `IRQ_MASK`/`IRQ_STATUS` mechanism decides which pending
events assert the 6502 IRQ line. The exact control-page layout is defined in
[video-output.md](video-output.md).

| Event | Meaning | Dirty-map effect | Programmer-visible effect |
| --- | --- | --- | --- |
| 6502 writes syncable video memory | live MIA RAM changed | set the page bit in the active map | no packet is sent yet |
| client sends `REQUEST_FRAME` with no pending response | client asks for the next update | active map becomes pending; already-clear other map becomes active | set `MIA_STAT_VIDEO_FRAME_REQUESTED`; set `IRQ_VIDEO_FRAME_REQUEST` |
| first `FRAME_DATA` send completes | every chunk was sent once | pending map is retained for possible repair | set `MIA_STAT_VIDEO_FRAME_SENT`; set `IRQ_VIDEO_FRAME_SENT` |
| client sends `NACK_CHUNKS` | some chunks were missed | requested chunks are regenerated from the pending map | status bits stay unchanged |
| client sends `ACK_RESPONSE` | client applied the complete update | pending map is cleared before it is released | clear `MIA_STAT_VIDEO_FRAME_REQUESTED` and `MIA_STAT_VIDEO_FRAME_SENT`; set `IRQ_VIDEO_FRAME_ACKED` after cleanup |
| reconnect or protocol-error restart | client mirror must be rebuilt | `HELLO` marks all syncable video page bits dirty | next update is a full refresh |

MIA tracks two dirty-map roles:

| Role | Meaning |
| --- | --- |
| active | receives bits for new 6502 writes |
| pending | dirty-page list for the current response, retained for repair |

Each write through an indexed window into the video memory range sets one bit in
the active dirty map, except writes to page 0. Rewriting the same syncable page
sets the same bit again; MIA keeps only one dirty record per page per response.

When no response is pending, only the active map can contain unsent page bits.
The other map is clear and ready for the next accepted request. Accepting a
request swaps the roles: the map that had been receiving writes becomes the
pending map for that response, and the clear map becomes active for later
writes.

When MIA accepts a `REQUEST_FRAME`, the dirty maps move through these steps:

1. if the active dirty map has no syncable page bits set, MIA returns
   `STATUS(NO_DIRTY_PAGES)`;
2. otherwise, MIA rotates the active map into the pending role and the
   already-clear other map into the active role;
3. MIA assigns a new nonzero `frame_id`;
4. MIA sends `FRAME_DATA` chunks for the pages set in the pending dirty map, in
   ascending page order.

The pending dirty map is retained until the response is acknowledged, implicitly
acknowledged by a later request, or a new `HELLO` resets the session. During that
time, `NACK_CHUNKS` and same-request retries regenerate chunks from the same
pending dirty map and the same deterministic page order.

When `ACK_RESPONSE` is accepted, MIA clears the pending map before clearing
`MIA_STAT_VIDEO_FRAME_REQUESTED`, clearing `MIA_STAT_VIDEO_FRAME_SENT`, setting
`IRQ_VIDEO_FRAME_ACKED`, or triggering the video IRQ. A 6502 program that sees
the acknowledgement event therefore sees a coherent all-clear state: new writes
are still tracked in the active map, there is no pending response, and the other
map has already been cleared for the next request.

MIA does not freeze page byte values for a response. Initial sends, retries, and
repairs may read current MIA RAM values for the pending page indexes. This keeps
the core 1 bus path fast: it only writes the live byte and marks the active
dirty bit. The visible consequence is that 6502 writes to visible video memory
during an active or unacknowledged update can appear in that update or in a
later repair of that update.

This behavior is intentional. Programs that need artifact-free output wait until
the client acknowledges the previous update before changing visible memory.
Programs that prefer lower latency can write earlier and accept possible
transient artifacts. Later updates converge the client mirror because every
post-request write marks the active dirty map for the next response.

## Request Handling

MIA tracks `client_frame_id`, the newest frame id that it believes the client
has completed. It starts at `0` for each session. Frame id comparisons in this
section use normal unsigned integer comparison.

When no response is pending:

- `REQUEST_FRAME(last_complete_frame_id == client_frame_id)` is valid. MIA
  either sends a new `FRAME_DATA` response or returns `STATUS(NO_DIRTY_PAGES)`.
- `REQUEST_FRAME(last_complete_frame_id < client_frame_id)` is stale. MIA
  ignores the request and does not create a response. This is normally a
  delayed or duplicate packet for a response MIA has already released. If a live
  client is actually behind, it discovers that by request timeout and restarts
  with `HELLO`.
- `REQUEST_FRAME(last_complete_frame_id > client_frame_id)` is impossible. MIA
  returns `STATUS(PROTOCOL_ERROR)`, invalidates the current video session, and
  waits for the client to restart with `HELLO`.

When a response is pending:

- `ACK_RESPONSE` matching the pending `request_id` and `frame_id` clears the
  pending dirty map, releases pending response state, and sets
  `client_frame_id = frame_id`.
- `ACK_RESPONSE` for an older, duplicate, or unknown response is ignored.
- `ACK_RESPONSE` for a future or impossible `frame_id` is a protocol error. MIA
  returns `STATUS(PROTOCOL_ERROR)`, invalidates the current video session, and
  waits for the client to restart with `HELLO`.
- `REQUEST_FRAME` with the same `request_id` and the same
  `last_complete_frame_id` as the pending response regenerates or resends that
  pending response.
- `REQUEST_FRAME(last_complete_frame_id == pending_frame_id)` is an implicit
  acknowledgement of the pending response. MIA clears the pending dirty map,
  releases pending response state, sets `client_frame_id = pending_frame_id`,
  and then handles the new request.
- `REQUEST_FRAME(last_complete_frame_id > pending_frame_id)` is impossible
  because MIA has not assigned or completed that newer frame. MIA returns
  `STATUS(PROTOCOL_ERROR)`, invalidates the current video session, and waits for
  the client to restart with `HELLO`.
- Other stale, duplicate, or early `REQUEST_FRAME` packets while a response is
  pending are ignored and do not create another response.

After `STATUS(PROTOCOL_ERROR)`, the client discards the current video session and
sends `HELLO`. MIA handles that `HELLO` as a normal session reset: it discards
pending response state, assigns a fresh `session_id`, marks every syncable video
page dirty, and returns `WELCOME`. The next valid `REQUEST_FRAME` produces a
full refresh.

## Repairs

The client sends `NACK_CHUNKS` when a chunked response is incomplete after its
repair timeout. This usually means one or more UDP datagrams were lost on the
network. It can also happen when a datagram was discarded by the Wi-Fi/IP/UDP
stack due to link corruption, checksum failure, receive-buffer pressure, or when
a delayed/reordered datagram arrives after the client's repair timeout.

The client detects missing chunks from the `FRAME_DATA` headers. Every chunk in
a response carries the same `request_id`, the response `frame_id`, its
zero-based `chunk_index`, and the response `chunk_count`. After receiving at
least one chunk, the client tracks which indexes from `0` through
`chunk_count - 1` have arrived. If the repair timeout expires before all indexes
are present, the client sends the absent indexes in `NACK_CHUNKS`. If zero chunks
arrive for a request, the client does not know the response
`frame_id` or `chunk_count`; it retries the same `REQUEST_FRAME` with the same
`request_id` and `last_complete_frame_id` instead. If repeated retries receive no
video response packet, the client resets the session with `HELLO`.

MIA repairs by regenerating the requested chunks from the pending dirty map. The
same `chunk_index` always maps to the same range of pending dirty page records
because records are fixed-size and ordered by ascending page index. Regenerated
repair chunks may contain newer byte values than an earlier send of the same
chunk if the 6502 wrote those pages while the response was pending.

If `NACK_CHUNKS` names the current pending response and all missing indexes are
valid, MIA sends those `FRAME_DATA` chunks again. If it names an old, duplicate,
or unknown response, MIA ignores the packet. If it names a future or impossible
response, MIA returns `STATUS(PROTOCOL_ERROR)`, discards pending response state,
invalidates the current video session, and waits for the client to restart with
`HELLO`. Unknown repairs never release pending response state.

## Full Refresh

A full refresh is a normal `FRAME_DATA` response whose pending dirty map has
every syncable video page bit set. MIA creates one by setting page bits `1`
through `2154` in the active dirty map before the next accepted
`REQUEST_FRAME`.

It is used:

- after `WELCOME` for the first client update,
- when a client reconnects,
- after an unrecoverable protocol mismatch,
- when the 6502 program explicitly requests a full refresh through MIA control,
- when firmware diagnostics decide the client mirror should be rebuilt.

A full refresh is not a frozen snapshot. MIA still reads live MIA RAM while
generating the response. Writes during the full refresh are tracked in the
active dirty map and will be sent again in a later update.

## Validation

Control packets have fixed payload lengths except `NACK_CHUNKS`:

| Packet | Valid `payload_len` |
| --- | ---: |
| `HELLO` | 0 |
| `WELCOME` | 0 |
| `REQUEST_FRAME` | 4 |
| `ACK_RESPONSE` | 0 |
| `NACK_CHUNKS` | `4 + 2 * missing_count` |
| `STATUS` | 2 |

`FRAME_DATA` `payload_len` must be a nonzero multiple of 34. It must contain no
more than 14 page records. `chunk_count` must be nonzero,
`chunk_index` must be less than `chunk_count`, and the final chunk must contain
at least one record.

For `NACK_CHUNKS`, `missing_count` must be greater than zero, `payload_len` must
equal `4 + 2 * missing_count`, every missing index must be less than the pending
response's `chunk_count`, and duplicate missing indexes are malformed.

A receiver rejects a packet or response as malformed when any of these are true:

- `magic`, `version`, `payload_len`, `flags`, or reserved fields are invalid;
- `session_id` is missing or wrong for packets that require an active session;
- a packet that must have no payload has a nonzero `payload_len`;
- a multi-byte field is out of range;
- `FRAME_DATA` page indexes are page 0, out of range, duplicated, or not
  strictly increasing across the complete response;
- `FRAME_DATA` chunk metadata disagrees across chunks for the same response;
- a control packet uses a nonzero unused header field that must be zero.

If MIA rejects a client packet for a known session, it sends
`STATUS(PROTOCOL_ERROR)` when doing so is useful and safe. If a client rejects a
MIA response as malformed, it discards the partial response and sends
`STATUS(PROTOCOL_ERROR)` when it can identify the session. After either side
reports `PROTOCOL_ERROR`, the client restarts with `HELLO`. If the client cannot
identify a valid session, it also restarts with `HELLO`.

## Timing and Pacing

The client owns the video request cadence. It requests at its chosen FPS while
the previous response is complete, acknowledged, or known to be absent. If an
update is large, repair-heavy, or delayed by Wi-Fi/lwIP backpressure, the next
request naturally occurs later.

Thirty request/acknowledgement cycles per second are small control traffic. The
limiting factor is the amount of dirty page data per response, not the control
packet rate. Normal gameplay updates are expected to dirty a small subset of
video memory. Full refreshes and large CHR uploads can take many packets and
should not be expected to sustain 30 FPS.

MIA does not expose a configurable send-rate limit in the protocol. Firmware may
still bound per-service work, defer sends when pbufs are unavailable, and respect
Wi-Fi/lwIP backpressure. Those are implementation safeguards, not wire
parameters.
