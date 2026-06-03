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
| Record data per packet | 480 B | payload minus 32 B | at default payload size |
| Max in-flight responses | 1 | 1-4 | outstanding frame responses requested by client |
| Repair timeout | 100 ms | 30-500 ms | delay before `NACK_CHUNKS` |
| Snapshot size | 68,944 B | fixed | 144 chunks at 480 data bytes per chunk |

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
| 4 | 4 | `session_id` | random id assigned by MIA after `HELLO` |
| 8 | 4 | `seq` | sender sequence number |
| 12 | 4 | `ack` | most recent peer sequence observed by sender |
| 16 | 4 | `frame_id` | video frame id, or `0` for non-frame messages |
| 20 | 2 | `request_id` | client request id, echoed by MIA |
| 22 | 2 | `chunk_index` | zero-based chunk number for chunked responses |
| 24 | 2 | `chunk_count` | chunk count for this response |
| 26 | 2 | `payload_len` | bytes after this header |
| 28 | 2 | `flags` | type-specific flags |
| 30 | 2 | `reserved` | sender writes zero; receiver ignores |

`seq` increments independently for each sender. `request_id` groups chunks that
belong to the same client request.

## Packet Types

| Type | Name | Direction | Purpose |
| ---: | --- | --- | --- |
| `0x01` | `HELLO` | client to MIA | start or resume a session |
| `0x02` | `WELCOME` | MIA to client | accept client and assign `session_id` |
| `0x03` | `SET_PARAMS` | client to MIA | request FPS, in-flight count, payload size, repair timeout |
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

The client keeps at most `max_in_flight` frame responses outstanding. A response
is outstanding from `REQUEST_FRAME` until the client receives every chunk and
sends `ACK_RESPONSE`, or until the client abandons the response and resyncs.

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

## Client-To-MIA Data

The client does not send video memory, pixels, CHR data, nametables, palettes,
or sprite state to MIA. MIA is the only source of video state.

Client-to-MIA packets are control and reliability messages:

| Packet | Client Data |
| --- | --- |
| `HELLO` | protocol version and session resume token |
| `SET_PARAMS` | requested FPS, payload size, in-flight count, repair timeout |
| `REQUEST_SNAPSHOT` | request id |
| `REQUEST_FRAME` | request id and `last_complete_frame_id` |
| `ACK_RESPONSE` | completed request id and frame id |
| `NACK_CHUNKS` | missing chunk indexes for one response |
| `STATUS` | keepalive and client diagnostics |

`REQUEST_FRAME` payload:

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 4 | `last_complete_frame_id` | newest frame fully applied by the client |
| 4 | 1 | `flags` | request flags; zero for normal frame requests |
| 5 | 3 | reserved | zero |

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

`SNAPSHOT_DATA` and `FRAME_DATA` payloads contain one or more records.

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

## Snapshot Response

A snapshot response contains enough records to rebuild the complete client
mirror.

The client applies a snapshot only after it has all chunks for the response.
Missing chunks are repaired with `NACK_CHUNKS`. Failed repair causes a fresh
`REQUEST_SNAPSHOT`.

MIA streams snapshot chunks across many calls to `mia_video_service()`. It does
not allocate or queue the entire snapshot at once.

## Frame Response

A frame response contains absolute records for the frame MIA selected for the
request.

The client applies a frame response only when its base state is known. The base
state is the `last_complete_frame_id` sent in `REQUEST_FRAME`.

For each `REQUEST_FRAME`, MIA returns one of these outcomes:

| Outcome | Meaning |
| --- | --- |
| exact next frame | records move the client from its base frame to the next queued frame |
| cumulative newer frame | records move the client from its base frame to a newer retained frame |
| resync required | MIA cannot build a response from the client's base frame |

If MIA cannot prove that the response is valid from the client's base frame, it
sends `STATUS` with the resync flag set. The client then requests a full
snapshot and does not apply partial frame data.

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

The client renders a frame only after every chunk for that response has arrived
and has been applied.

## Repairs

The client sends `NACK_CHUNKS` when a chunked response is incomplete after
`repair_timeout_ms`. This usually means one or more UDP datagrams were lost on
the network. It can also happen when a datagram was discarded by the Wi-Fi/IP/UDP
stack due to link corruption, checksum failure, receive-buffer pressure, or when
a delayed/reordered datagram arrives after the client's repair timeout.

Payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | `missing_count` |
| 2 | 2 * N | missing chunk indexes |

MIA retransmits retained missing chunks. If the requested response is no longer
retained, MIA sends `STATUS` with the resync flag set. The client then requests
a new snapshot.

## In-Flight Responses

`max_in_flight` is the number of requested frame responses the client allows to
exist at once.

Higher values improve throughput on higher-latency links because MIA keeps
sending while older responses are still traveling. Higher values also increase
memory pressure and visual lag. The accepted maximum is 4.

## Frame Ids

`frame_id` increments on every accepted `VIDEO_COMMIT_FRAME`.

A local/free-running program can produce gaps at the client when MIA coalesces
old unsent frame state. A video-paced program that waits for
`VIDEO_CAN_COMMIT` does not produce intentional gaps.

## Session Ownership

MIA accepts one active client. A second `HELLO` receives a `STATUS` response
with the busy flag while the active session is alive. If the active session has
timed out, the next valid `HELLO` becomes the active client and receives a new
`session_id`.

Discovery and trust are local-network mechanisms. The protocol payload includes
the session id; it does not provide encryption.
