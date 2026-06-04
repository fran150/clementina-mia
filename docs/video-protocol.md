# MIA Video Protocol

This document defines the client/MIA wire protocol for Wi-Fi video output. It
matches the `docs/video-poc` architecture: client mirror, initial snapshot,
absolute updates, client-requested frames, repair, and bounded in-flight frame
responses.

The protocol serves one active client at a time.

## Transport

Video uses UDP over IPv4.

UDP keeps latency and firmware complexity low. Packet loss is handled in the
application protocol through chunking, complete-response acknowledgements,
missing-chunk repair, and snapshot resync. Payload integrity is provided by the
link layer and UDP checksum; corrupted datagrams are treated as missing packets.

## Defaults and Limits

| Parameter | Default | Accepted Range | Notes |
| --- | ---: | ---: | --- |
| Client FPS | 25 | 5-30 | rate of client `REQUEST_FRAME` messages |
| Max UDP payload | 512 B | 256-512 B | includes the 32-byte protocol header |
| Protocol header | 32 B | fixed | every packet starts with this header |
| Response stream bytes per packet | 480 B | payload minus 32 B | at default payload size |
| Max in-flight responses | 1 | 1-4 | outstanding frame responses requested by client |
| Repair timeout | 100 ms | 30-500 ms | delay before `NACK_CHUNKS` |
| Bandwidth hint | 0 KiB/s | 0, 16-4096 KiB/s | `0` means unrestricted |
| Keepalive interval | 500 ms | fixed | client sends `STATUS_KEEPALIVE` when otherwise idle |
| Session timeout | 3,000 ms | fixed | MIA expires an idle active client |
| Snapshot mirror size | 68,944 B | fixed | mirror data alone is 144 chunks at the default payload size |

The client requests parameters with `SET_PARAMS`. MIA responds with the accepted
values in `PARAMS_ACCEPTED` and uses those values until the session ends.

## Endianness

All multi-byte fields are little-endian.

## Packet Header

Every packet starts with a 32-byte header:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `magic` | `0x4D56` (`"MV"` little-endian) |
| 2 | 1 | `version` | protocol version, `1` |
| 3 | 1 | `type` | packet type |
| 4 | 4 | `session_id` | nonzero random id assigned by MIA after `HELLO` |
| 8 | 4 | `seq` | nonzero sender sequence number |
| 12 | 4 | `ack` | highest peer sequence observed by sender |
| 16 | 4 | `frame_id` | nonzero 32-bit video frame id, or `0` for non-frame/unknown-target messages |
| 20 | 2 | `request_id` | 16-bit client request id, echoed by MIA |
| 22 | 2 | `chunk_index` | zero-based chunk number for chunked responses |
| 24 | 2 | `chunk_count` | chunk count for this response |
| 26 | 2 | `payload_len` | bytes after this header |
| 28 | 2 | `flags` | type-specific flags |
| 30 | 2 | `reserved` | sender writes zero; receiver validates zero |

`seq` increments independently for each sender. `seq = 0` is reserved as the
empty `ack` sentinel and is never sent as a packet sequence number. `request_id`
groups chunks that belong to the same client request.

## Packet Types

| Type | Name | Direction | Purpose |
| ---: | --- | --- | --- |
| `0x01` | `HELLO` | client to MIA | start a session |
| `0x02` | `WELCOME` | MIA to client | accept client and assign nonzero `session_id` |
| `0x03` | `SET_PARAMS` | client to MIA | request FPS, in-flight count, payload size, repair timeout, bandwidth hint |
| `0x04` | `PARAMS_ACCEPTED` | MIA to client | return accepted transport parameters |
| `0x05` | `REQUEST_FRAME` | client to MIA | ask for the next frame response |
| `0x06` | `ACK_RESPONSE` | client to MIA | acknowledge a complete response |
| `0x07` | `NACK_CHUNKS` | client to MIA | request missing chunks |
| `0x08` | `REQUEST_SNAPSHOT` | client to MIA | ask for a full state snapshot |
| `0x10` | `SNAPSHOT_DATA` | MIA to client | chunk of a full snapshot response |
| `0x20` | `FRAME_DATA` | MIA to client | chunk of a frame update response |
| `0x30` | `STATUS` | either | keepalive, counters, diagnostics, resync flags |

## Session Flow

Startup:

```text
client -> MIA: HELLO
MIA    -> client: WELCOME(session_id)
client -> MIA: SET_PARAMS
MIA    -> client: PARAMS_ACCEPTED
client -> MIA: REQUEST_SNAPSHOT
MIA    -> client: SNAPSHOT_DATA chunks
client -> MIA: ACK_RESPONSE
```

Steady state:

```text
client -> MIA: REQUEST_FRAME(request_id, last_complete_frame_id)
MIA    -> client: FRAME_DATA chunks
client -> MIA: ACK_RESPONSE(request_id, frame_id)
```

If there is no newer frame, MIA returns `STATUS_KEEPALIVE` with reason
`NO_NEWER_FRAME` and no `ACK_RESPONSE` is required. If resync is required, MIA
returns `STATUS_RESYNC_REQUIRED` and the client requests a snapshot.

The client keeps at most `max_in_flight` frame responses outstanding. A response
is outstanding from `REQUEST_FRAME` until the client receives every chunk and
sends `ACK_RESPONSE`, sends `ACK_RESPONSE` for a stale response it discarded, or
abandons the response and resyncs. A `REQUEST_FRAME` that receives `STATUS`
with `STATUS_KEEPALIVE` and reason `NO_NEWER_FRAME` does not create an
outstanding frame response.

Version 1 does not support session resume. `HELLO` has no payload, uses
`session_id = 0`, and starts a new session. `WELCOME` has no payload; the
assigned nonzero session id is carried in the packet header. A reconnecting
client requests a new snapshot after `WELCOME`.

MIA never assigns `session_id = 0`. The value `0` is reserved for `HELLO`,
packets without an established session, and `STATUS` responses that intentionally
do not identify an active session.

The active client is identified by both its UDP source endpoint and the assigned
`session_id`. The endpoint is the source IPv4 address and UDP port that sent the
accepted `HELLO`. MIA refreshes the session timeout only for valid packets from
that endpoint with the active `session_id`, except for the initial `HELLO` with
`session_id = 0`. A packet from another endpoint carrying the active
`session_id` is treated as a different client and receives `STATUS_BUSY` while
the original session is alive.

`request_id` is a 16-bit client-generated id. The client increments it modulo
`65536` for each `REQUEST_SNAPSHOT` and `REQUEST_FRAME`. It must not reuse a
`request_id` while any previous response with that id is outstanding, repairable,
or inside the 3,000 ms late-packet guard after a terminal `ACK_RESPONSE`
(`ACK_APPLIED`, `ACK_STALE`, or `ACK_ABANDONED`). With the accepted 30 FPS
maximum, wraparound is slow enough that this guard is normally automatic. MIA and
the client identify a known response by `(request_id, response_type, frame_id)`
and discard chunks that do not match an active known response.

## Header Use by Packet Type

Every sender initializes `seq` to `1`, increments it for each packet it sends,
and wraps from `0xFFFFFFFF` to `1`. `ack` is the highest peer `seq` observed by
that sender using 32-bit serial number comparison, or `0` if none has been
observed. Sequence `a` is newer than sequence `b` when
`0 < (uint32_t)(a - b) < 0x80000000`.

Valid frame ids are nonzero and also use 32-bit serial number comparison. All
protocol text that says a frame id is newer, older, `<=`, `>=`, `<`, or `>` uses
that serial ordering, not plain integer ordering. MIA increments `FRAME_ID` on
each accepted `VIDEO_COMMIT_FRAME`, wrapping from `0xFFFFFFFF` to `1`.
`frame_id = 0` is reserved for non-frame packets and unknown-target abandon; it
is never an accepted frame id and never participates in frame ordering.

`payload_len` is meaningful for every packet and is set to the number of bytes
after the 32-byte packet header. Packets with no payload set `payload_len = 0`.

All other fields not listed as meaningful for a packet type are written as zero
by the sender. Reserved fields are validated as zero; non-reserved unused fields
are ignored by the receiver.

`seq` and `ack` are diagnostics and liveness aids only. They do not control
delivery, retransmission, frame ordering, chunk repair, or client mirror
application. Request ids, response chunk indexes, and frame ids define protocol
state.

| Packet | `session_id` | `frame_id` | `request_id` | `chunk_index/count` | `flags` |
| --- | --- | --- | --- | --- | --- |
| `HELLO` | `0` | `0` | `0` | `0/0` | `0` |
| `WELCOME` | assigned nonzero session | `0` | `0` | `0/0` | `0` |
| `SET_PARAMS` | active session | `0` | `0` | `0/0` | `0` |
| `PARAMS_ACCEPTED` | active session | `0` | `0` | `0/0` | bit 0 `PARAMS_CLAMPED` |
| `REQUEST_SNAPSHOT` | active session | `0` | client request id | `0/0` | `0` |
| `REQUEST_FRAME` | active session | `0` | client request id | `0/0` | `0` |
| `SNAPSHOT_DATA` | active session | snapshot frame id | echoed request id | response chunk position | `0` |
| `FRAME_DATA` | active session | target frame id | echoed request id | response chunk position | `0` |
| `ACK_RESPONSE` | active session | acknowledged response frame id, or `0` for unknown-target abandon | acknowledged request id | `0/0` | one `ACK_*` bit |
| `NACK_CHUNKS` | active session | response frame id | response request id | `0/0` | `0` |
| `STATUS` | active session, or `0` if no session | latest frame id, or `0` | related request id, or `0` | `0/0` | `STATUS_*` bits |

`PARAMS_CLAMPED = 0x0001` means at least one requested parameter was outside the
accepted range and was clamped in `PARAMS_ACCEPTED`.

`ACK_RESPONSE` flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `ACK_APPLIED` | client applied the complete response |
| 1 | `ACK_STALE` | client received the complete response but discarded it because its `frame_id` was not newer than the client mirror |
| 2 | `ACK_ABANDONED` | client abandoned the response, usually because it will request a snapshot |

Exactly one `ACK_*` bit is set.

`STATUS` flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `STATUS_RESYNC_REQUIRED` | receiver must discard partial frame state and request a snapshot |
| 1 | `STATUS_BUSY` | MIA has an active session owned by another client |
| 2 | `STATUS_KEEPALIVE` | no state change; liveness/diagnostics only |
| 3 | `STATUS_RESPONSE_NOT_RETAINED` | requested repair/ack target is no longer retained |
| 4 | `STATUS_PROTOCOL_ERROR` | malformed packet, bad field value, or unsupported version |

## Client Parameters

`SET_PARAMS` and `PARAMS_ACCEPTED` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 1 | `target_fps` | frame request rate |
| 1 | 1 | `max_in_flight` | in-flight frame response limit |
| 2 | 2 | `max_payload` | maximum UDP payload bytes, including header |
| 4 | 2 | `repair_timeout_ms` | delay before missing chunk repair |
| 6 | 2 | `bandwidth_kib_s` | bandwidth hint; `0` means unrestricted |

MIA clamps each field to the accepted range and returns the exact values it will
use. The client uses those accepted values for pacing, timeout decisions, and
packet sizing.

`bandwidth_kib_s = 0` disables bandwidth pacing. A nonzero value is clamped to
`16-4096` KiB/s. MIA enforces the accepted value with a token bucket over
application UDP payload bytes, including the 32-byte protocol header. If the
bucket is empty, `mia_video_service()` delays sending more chunks; retained frame
and snapshot state remain subject to the normal staging-memory limits.

## Client-To-MIA Data

The client does not send video memory, pixels, CHR data, nametables, palettes,
or sprite state to MIA. MIA is the only source of video state.

Client-to-MIA packets are control and reliability messages:

| Packet | Client Data |
| --- | --- |
| `HELLO` | no payload in version 1 |
| `SET_PARAMS` | requested FPS, payload size, in-flight count, repair timeout, bandwidth hint |
| `REQUEST_SNAPSHOT` | request id in packet header |
| `REQUEST_FRAME` | request id and `last_complete_frame_id` |
| `ACK_RESPONSE` | completed request id and frame id |
| `NACK_CHUNKS` | response id and missing chunk indexes |
| `STATUS` | keepalive and client diagnostics |

`REQUEST_FRAME` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 4 | `last_complete_frame_id` | newest frame fully applied by the client |
| 4 | 1 | `flags` | request flags; zero for normal frame requests |
| 5 | 3 | reserved | zero |

`ACK_RESPONSE` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 1 | `response_type` | `SNAPSHOT_DATA` (`0x10`) or `FRAME_DATA` (`0x20`) |
| 1 | 1 | `ack_status` | `0` applied, `1` stale, `2` abandoned |
| 2 | 2 | reserved | zero |
| 4 | 4 | `client_frame_id` | newest frame id fully represented by the client mirror after this ack |

`ack_status` must match the single `ACK_*` bit set in the packet header.
For `ACK_ABANDONED`, the packet header `frame_id` is the abandoned response's
frame id when known. If the client received zero chunks for that response and
therefore never learned the response target, it sends `frame_id = 0`.
`ACK_ABANDONED` with `frame_id = 0` is the only unknown-target ack form. It
matches by `(request_id, response_type)` instead of the normal
`(request_id, response_type, frame_id)` tuple and releases any outstanding
response for that request/type if MIA still has one.

`NACK_CHUNKS` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 1 | `response_type` | `SNAPSHOT_DATA` (`0x10`) or `FRAME_DATA` (`0x20`) |
| 1 | 1 | reserved | zero |
| 2 | 2 | `missing_count` | number of missing chunk indexes |
| 4 | 2 * N | `missing_indexes` | zero-based chunk indexes for the response named by `request_id`, `frame_id`, and `response_type` |

`STATUS` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | `status_flags` | same bit values as the packet header `flags` |
| 2 | 2 | `reason` | status reason code |
| 4 | 4 | `latest_frame_id` | newest frame id known to the sender, or `0` |
| 8 | 4 | `retained_base_frame_id` | oldest frame id from which MIA can build a frame response, or `0` |
| 12 | 4 | `sender_time_ms` | low 32 bits of sender uptime in milliseconds |

For `STATUS_BUSY`, `session_id` is `0` unless MIA is willing to disclose the
active session id. For `STATUS_RESYNC_REQUIRED`, `request_id` and `frame_id`
identify the frame response or repair target that failed when applicable.

Status reason codes:

| Code | Name |
| ---: | --- |
| `0` | `NONE` |
| `1` | `BUSY` |
| `2` | `STALE_BASE` |
| `3` | `RESPONSE_NOT_RETAINED` |
| `4` | `SNAPSHOT_ACTIVE` |
| `5` | `PROTOCOL_ERROR` |
| `6` | `TIMEOUT` |
| `7` | `NO_NEWER_FRAME` |

## Control Packet Validation

Control packets have fixed payload lengths except `NACK_CHUNKS`:

| Packet | Required `payload_len` |
| --- | ---: |
| `HELLO` | 0 |
| `WELCOME` | 0 |
| `SET_PARAMS` | 8 |
| `PARAMS_ACCEPTED` | 8 |
| `REQUEST_SNAPSHOT` | 0 |
| `REQUEST_FRAME` | 8 |
| `ACK_RESPONSE` | 8 |
| `NACK_CHUNKS` | `4 + 2 * missing_count` |
| `STATUS` | 16 |

A receiver rejects a control packet as malformed when any required reserved byte
or reserved header field is nonzero, when `payload_len` does not match the
packet type, or when the packet uses flags not defined for that packet type.
For `STATUS`, the payload `status_flags` must equal the packet header `flags`.

For `NACK_CHUNKS`, `missing_count` must be greater than zero, `payload_len` must
equal `4 + 2 * missing_count`, and every missing index must be less than the
retained response's `chunk_count`. Duplicate missing indexes are malformed. A
`NACK_CHUNKS` for an unknown response is not repairable; MIA responds with
`STATUS_RESYNC_REQUIRED` and `STATUS_RESPONSE_NOT_RETAINED`.

## State Regions

Update records address bytes within named regions. Region layout and control
fields are defined in [video-output.md](video-output.md).

| Region | Id | Size | Contents |
| --- | ---: | ---: | --- |
| `CONTROL` | `0x00` | 256 B | mode, status, frame id, scroll, active banks, flags |
| `PALETTE` | `0x01` | 256 B | 16 banks of 8 RGB565 colors |
| `CHR` | `0x02` | 49,152 B | 8 character banks |
| `BG_NT` | `0x03` | 8,000 B | background nametables |
| `BG_ATTR` | `0x04` | 8,000 B | background attributes |
| `OV_NT` | `0x05` | 1,000 B | overlay nametable |
| `OV_ATTR` | `0x06` | 1,000 B | overlay attributes |
| `OAM` | `0x07` | 1,280 B | sprite records |

## Update Records

`SNAPSHOT_DATA` and `FRAME_DATA` chunks carry consecutive bytes of one response
stream. The response stream starts with a fixed prefix, followed by update
records:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 4 | `base_frame_id` | frame id the response is valid from |
| 4 | 4 | `stream_len` | total response stream bytes, including this prefix |
| 8 | 2 | `record_count` | number of update records after the prefix |
| 10 | 2 | reserved | zero |

For `SNAPSHOT_DATA`, `base_frame_id` equals the snapshot frame id in the packet
header. For `FRAME_DATA`, `base_frame_id` equals the `last_complete_frame_id`
from the matching `REQUEST_FRAME`.

Records may cross chunk boundaries. The client reassembles all chunks for a
response before parsing the response stream.

For a response with accepted `max_payload`, the chunk data capacity is:

```text
chunk_capacity = max_payload - 32
```

The byte offset of a chunk in the response stream is:

```text
chunk_offset = chunk_index * chunk_capacity
```

All chunks except the final chunk have `payload_len = chunk_capacity`. The final
chunk has `payload_len = stream_len - chunk_offset`. `chunk_count` is
`ceil(stream_len / chunk_capacity)`.

Record header:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 1 | `record_type` | `0x01` write, `0x02` fill |
| 1 | 1 | `region_id` | target region |
| 2 | 3 | `offset` | byte offset within region |
| 5 | 2 | `length` | number of bytes affected |
| 7 | N | `data` | write data, or one fill byte for fill records |

`WRITE` records carry `length` data bytes.

`FILL` records carry one data byte and write that value `length` times.

Records are absolute. Applying a record sets the client mirror bytes to the
specified values. The client never needs the previous value.

## Stream Validation

A receiver rejects a packet or response stream as malformed when any of these
conditions are true:

- `magic`, `version`, `session_id`, reserved fields, or `payload_len` are invalid.
- `payload_len` exceeds the accepted `max_payload - 32`.
- `chunk_count` is zero for `SNAPSHOT_DATA` or `FRAME_DATA`.
- `chunk_index >= chunk_count`.
- chunks for the same response disagree on `frame_id`, `request_id`,
  `chunk_count`, or response type.
- a non-final response chunk has `payload_len != chunk_capacity`.
- the final response chunk's `payload_len` does not match `stream_len`.
- the response stream is shorter than the 12-byte stream prefix.
- `stream_len` does not match the reassembled response byte count.
- `record_count` does not match the parsed record count.
- record parsing does not consume exactly `stream_len` bytes.
- `record_type` is unknown.
- `region_id` is unknown.
- `length` is zero.
- `offset + length` exceeds the target region size.
- a `WRITE` record does not contain exactly `length` data bytes.
- a `FILL` record does not contain exactly one fill byte.

Rejected records are never applied. If MIA rejects a client packet, it sends
`STATUS` with `STATUS_PROTOCOL_ERROR` when the session is known. If the client
rejects a MIA response, it sends `ACK_RESPONSE` with `ACK_ABANDONED` when it can
identify the request, discards partial response state, and requests a snapshot.

## Snapshot Response

A snapshot response contains enough records to rebuild the complete client
mirror.

The client applies a snapshot only after it has all chunks for the response.
Missing chunks are repaired with `NACK_CHUNKS`. Failed repair causes a fresh
`REQUEST_SNAPSHOT`.

When MIA accepts `REQUEST_SNAPSHOT`, it assigns `snapshot_frame_id` to the
current committed `FRAME_ID` and creates a stable snapshot source for the full
68,944-byte mirror at that frame id. Every byte in the `SNAPSHOT_DATA` response
must come from that same snapshot source. Writes and commits that happen after
the snapshot starts belong to later frame ids and are not part of the snapshot.

The stable source may be a copied snapshot buffer, a retained immutable mirror,
or another implementation that guarantees all streamed bytes represent
`snapshot_frame_id`. MIA does not need to allocate or queue all UDP packets for
the snapshot at once; it may still stream chunks across many calls to
`mia_video_service()`.

After applying a complete snapshot, the client sets its current frame id to
`snapshot_frame_id`, discards any retained incomplete frame responses with
`frame_id <= snapshot_frame_id`, and resumes by sending `REQUEST_FRAME` with
`last_complete_frame_id = snapshot_frame_id`.

## Frame Response

A frame response contains absolute records for the frame MIA selected for the
request.

The client applies a frame response only when its base state is known. The
response stream's `base_frame_id` is the `last_complete_frame_id` sent in
`REQUEST_FRAME`.

A `FRAME_DATA` response is cumulative from `base_frame_id` to the target
`frame_id` in the packet header. It must contain every byte in the union of video
ranges dirtied by accepted frames `(base_frame_id, frame_id]`, using the byte
values from the target `frame_id`. This makes the response safe to apply when
the client's current frame id is any value `>= base_frame_id` and `< frame_id`.

For each `REQUEST_FRAME`, MIA returns one of these outcomes:

| Outcome | Meaning |
| --- | --- |
| exact next frame | records move the client from its base frame to the next queued frame |
| cumulative newer frame | records move the client from its base frame to a newer retained frame |
| no newer frame | MIA has no accepted frame newer than the client's base frame |
| resync required | MIA cannot build a response from the client's base frame |

If MIA cannot prove that the response is valid from the client's base frame for
a reason not otherwise specified below, it sends `STATUS` with
`STATUS_RESYNC_REQUIRED` set. The client then requests a full snapshot and does
not apply partial frame data.

If `last_complete_frame_id` is older than the oldest retained base frame from
which MIA can build a cumulative frame response, MIA sends `STATUS` with
`STATUS_RESYNC_REQUIRED` set, reason `STALE_BASE`, `request_id` echoed from the
request, `frame_id`/`latest_frame_id` set to MIA's latest accepted frame, and
`retained_base_frame_id` set to that oldest retained base. The client then
requests a snapshot.

If `last_complete_frame_id` is newer than MIA's latest accepted `FRAME_ID`, the
client is claiming a future base frame. MIA sends `STATUS` with
`STATUS_RESYNC_REQUIRED` and `STATUS_PROTOCOL_ERROR` set, reason
`PROTOCOL_ERROR`, `request_id` echoed from the request, and
`frame_id`/`latest_frame_id` set to MIA's latest accepted frame. The client then
requests a snapshot.

If `last_complete_frame_id` equals MIA's latest accepted `FRAME_ID`, MIA sends
`STATUS` with `STATUS_KEEPALIVE`, reason `NO_NEWER_FRAME`, `request_id` echoed
from the request, and `frame_id`/`latest_frame_id` set to the latest frame. The
client treats that request as complete without sending `ACK_RESPONSE` and may
send another `REQUEST_FRAME` according to its accepted frame pacing.

The client never applies a frame response with `frame_id <= current_frame_id`.
That response is stale; the client discards it and sends `ACK_RESPONSE` with
`ACK_STALE`. If a complete frame response has `base_frame_id >
current_frame_id`, the client cannot prove it has the required base state. It
abandons pending frame responses and requests a snapshot.

Local/free-running program behavior:

- If the 6502 has committed multiple frames since the last request, MIA
  coalesces unsent state and sends the newest coherent state.
- The client can observe gaps in `frame_id`.
- The client mirror stays synchronized because every record is absolute.

Video-paced program behavior:

- The 6502 waits for `VIDEO_CAN_COMMIT`.
- MIA retains each accepted committed frame until it is requested, transmitted,
  acknowledged, repaired, or replaced by a snapshot resync.
- The client does not observe intentional frame-id gaps.

The client renders a frame only after every chunk for an applicable response has
arrived and the response has been applied.

## Repairs

The client sends `NACK_CHUNKS` when a chunked response is incomplete after
`repair_timeout_ms`. This usually means one or more UDP datagrams were lost on
the network. It can also happen when a datagram was discarded by the Wi-Fi/IP/UDP
stack due to link corruption, checksum failure, receive-buffer pressure, or when
a delayed/reordered datagram arrives after the client's repair timeout.

`NACK_CHUNKS` identifies the response with the packet header `request_id`,
packet header `frame_id`, and payload `response_type`. The missing indexes are
chunk indexes for that response stream, not record indexes.

The client sends `NACK_CHUNKS` only after receiving at least one chunk for the
response, because every response chunk carries the target `frame_id` and
`chunk_count`. If `repair_timeout_ms` elapses for an outstanding response and
the client has received zero chunks for that response, the target frame and chunk
count are unknown. The client sends `ACK_RESPONSE` with `ACK_ABANDONED`,
`frame_id = 0`, and the request id, then requests a snapshot instead of sending
`NACK_CHUNKS`.

MIA retransmits retained missing chunks. If the requested response is no longer
retained, MIA sends `STATUS` with `STATUS_RESYNC_REQUIRED` and
`STATUS_RESPONSE_NOT_RETAINED` set. The client then requests a new snapshot.

## In-Flight Responses

`max_in_flight` is the number of requested frame responses the client allows to
exist at once.

Higher values improve throughput on higher-latency links because MIA keeps
sending while older responses are still traveling. Higher values also increase
memory pressure and visual lag. The accepted maximum is 4.

Out-of-order completion is allowed. The client applies only responses whose
target `frame_id` is newer than the current client mirror and whose
`base_frame_id` is not newer than the current client mirror. A newer cumulative
response may make older in-flight responses stale. Stale responses are discarded
and acknowledged with `ACK_STALE` so MIA can release retained response state.

MIA must retain or regenerate response chunks until the response is acknowledged,
abandoned, replaced by snapshot resync, or no longer retained. If MIA cannot
repair or regenerate an outstanding response, it sends `STATUS` with
`STATUS_RESYNC_REQUIRED`.

## Keepalive and Timeout

Any valid packet from the active client endpoint with the active `session_id`
refreshes the session liveness timer. When the client has no other packet to
send for 500 ms, it sends `STATUS` with `STATUS_KEEPALIVE`. The keepalive
payload uses the normal `STATUS` schema and sets `latest_frame_id` to the newest
frame fully represented by the client mirror.

MIA expires the active session after 3,000 ms with no valid packet from that
endpoint and session id. Expiry releases retained responses, repair state, and
snapshot state for that session. After expiry, the next valid `HELLO` can become
the active client and receives a new `session_id`.

MIA may also send `STATUS_KEEPALIVE` while otherwise idle. Client-side timeout
policy is implementation-defined, but a client that decides the session is dead
must abandon outstanding responses and restart with `HELLO`.

## Frame Ids

MIA initializes `FRAME_ID` to `1` for the initial video state before serving
snapshots. `frame_id` increments on every accepted `VIDEO_COMMIT_FRAME`,
wrapping from `0xFFFFFFFF` to `1`. Frame ordering uses the serial comparison
rule defined in the packet-header section. `0` is not a valid accepted frame id.

A local/free-running program can produce gaps at the client when MIA coalesces
old unsent frame state. A video-paced program that waits for
`VIDEO_CAN_COMMIT` does not produce intentional gaps.

## Session Ownership

MIA accepts one active client endpoint at a time. A second endpoint's `HELLO`
receives a `STATUS` response with `STATUS_BUSY` set while the active session is
alive. If the active session has timed out, the next valid `HELLO` becomes the
active client and receives a new `session_id`.

Discovery and trust are local-network mechanisms. The protocol payload includes
the session id; it does not provide encryption.
