# MIA Video Output

This document defines MIA's Wi-Fi video-output architecture. The wire protocol
is defined in [video-protocol.md](video-protocol.md), and the 6502 programming
model is defined in [video-programmer-guide.md](video-programmer-guide.md).

MIA video uses a client-paced readout model:

- The client keeps a complete mirror of MIA video state.
- MIA divides the 68,944-byte video state into 2,155 pages of 32 bytes each.
- MIA tracks those pages with a 270-byte dirty map, where each bit represents
  one video page.
- The client requests updates at the pace its display and network can sustain.
- MIA keeps two dirty maps: one active map for new 6502 writes, and one pending
  map for the update currently being generated or repaired.
- When a request is accepted, MIA moves the active map into the pending role,
  makes the already-clear other map active for new writes, and sends the pages
  named by the pending map.
- While MIA is generating or repairing an update, it reads live MIA RAM.
- The 6502 program can use status bits and IRQ events to avoid changing visible
  memory during the readout/acknowledgement window.

MIA does not stream raw pixels and does not run a video codec. The 6502 writes a
compact tile/sprite state. The client renders the final pixels locally.

## Goals

- Render a 320x200 display on a host client over Wi-Fi.
- Keep the 6502 programming model close to classic tile/sprite machines.
- Avoid sending full pixel frames during normal rendering.
- Stay useful at 25-30 FPS for ordinary gameplay-style dirty updates.
- Recover from packet loss without permanent client desync.
- Keep the core 1 bus path bounded and fast: write live RAM, mark one dirty bit,
  and return.
- Let the 6502 program choose between artifact-tolerant free-running updates and
  stricter client-paced visible-memory updates.

## Non-Goals

- Physical VGA, HDMI, or composite output.
- Full-frame image compression on the Pico.
- Multiple simultaneous video clients.
- Stable per-frame snapshots of video memory.
- Guaranteed artifact-free output when the 6502 changes visible memory while
  MIA is reading it for an update or repair.

## Firmware Context

MIA appears to Clementina as a 32-byte register block at `$FFE0-$FFFF`. It owns
128 KiB of internal RAM and exposes that RAM through indexed windows.

Core split:

- Core 1 services the time-critical 6502 bus action loop.
- Core 0 runs the firmware service loop, Wi-Fi polling, dirty-map rotation,
  packet construction, repair, and video publishing.

The firmware executes most code from flash/XIP. The bus-critical path remains
RAM-resident:

- `act_loop()` is in RAM.
- The indexed memory helpers are in RAM or inlined into `act_loop()`.
- IRQ status evaluation used by the indexed write path is inlined into
  `act_loop()`.
- Generated `kernel_data` used by the loader is in RAM.
- The command trigger path writes the SIO FIFO directly from `act_loop()`.

The action loop does not build packets, poll Wi-Fi, allocate buffers, scan dirty
maps, or wait for core 0. It only services bus events, updates MIA
RAM/registers, marks the active video dirty bit, and pushes compact command
notifications to core 0.

## Video Memory Map

The video state occupies the first 68,944 bytes of MIA RAM. All offsets are byte
offsets from the start of MIA RAM.

| Offset | Size | Region | Purpose |
| ---: | ---: | --- | --- |
| `$00000` | 256 B | `CONTROL` | mode, status, frame id, scroll, active banks, flags |
| `$00100` | 256 B | `PALETTE` | 16 palette banks, 8 RGB565 colors each |
| `$00200` | 49,152 B | `CHR` | 8 banks, 256 characters per bank, 24 bytes per character |
| `$0C200` | 8,000 B | `BG_NT` | 8 background nametables, 40x25 bytes each |
| `$0E140` | 8,000 B | `BG_ATTR` | 8 background attribute tables, 40x25 bytes each |
| `$10080` | 1,000 B | `OV_NT` | fixed screen-space overlay nametable |
| `$10468` | 1,000 B | `OV_ATTR` | fixed screen-space overlay attributes |
| `$10850` | 1,280 B | `OAM` | 256 sprite records, 5 bytes each |
| `$10D50` | - | end | first byte after video state |

The full client mirror is 68,944 bytes, or 67.3 KiB. A full refresh sends all
2,155 dirty pages and is appropriate for startup and recovery. Normal updates
are expected to dirty a much smaller subset of the mirror.

## Control Block

The control block is little-endian. Fields marked read-only are written by MIA
and read by the 6502 program.

| Offset | Size | Field | Access | Meaning |
| ---: | ---: | --- | --- | --- |
| `$00` | 1 | `VIDEO_VERSION` | read-only | video state layout version |
| `$01` | 1 | `VIDEO_MODE` | read/write | video enable and renderer mode bits |
| `$02` | 1 | `VIDEO_STATUS` | read-only | connection and update lifecycle bits |
| `$03` | 1 | `LAYER_ENABLE` | read/write | background, overlay, and sprite enables |
| `$04` | 4 | `FRAME_ID` | read-only | latest assigned update/readout frame id |
| `$08` | 2 | `SCROLL_X` | read/write | background scroll X in pixels |
| `$0A` | 2 | `SCROLL_Y` | read/write | background scroll Y in pixels |
| `$0C` | 1 | `BG_ACTIVE_SET` | read/write | active 2x2 background set `0-1` |
| `$0D` | 1 | `BG_SCROLL_MODE` | read/write | background plane mode `0-3` |
| `$0E` | 1 | `BG_CHR_BANK` | read/write | primary background CHR bank `0-7` |
| `$0F` | 1 | `BG_ALT_CHR_BANK` | read/write | alternate background CHR bank `0-7` |
| `$10` | 1 | `OVERLAY_CHR_BANK` | read/write | primary overlay CHR bank `0-7` |
| `$11` | 1 | `OVERLAY_ALT_CHR_BANK` | read/write | alternate overlay CHR bank `0-7` |
| `$12` | 1 | `SPRITE_CHR_BANK` | read/write | sprite CHR bank `0-7` |
| `$13` | 1 | `BACKDROP_PALETTE` | read/write | backdrop palette/color selection |
| `$14` | 2 | `OAM_ACTIVE_COUNT` | read/write | active sprite record count; `0` means none |
| `$16` | 1 | `FRAME_FLAGS` | read/write | render flags sampled by the client |
| `$17` | 1 | reserved | - | zero |
| `$18` | 1 | `VIDEO_IRQ_ENABLE` | read/write | enabled video event IRQ sources |
| `$19` | 1 | `VIDEO_EVENT_STATUS` | read/write-1-clear | pending video event bits |
| `$1A` | 6 | reserved | - | zero |
| `$20` | 224 | reserved | - | zero |

`FRAME_ID` starts at `0` for a new session and increments when MIA accepts a
client update request that produces `FRAME_DATA`. It wraps from `0xFFFFFFFF` to
`1`; `0` is reserved for "no update applied yet."

`VIDEO_MODE` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_MODE_ENABLE` | video service enabled |
| 1 | `VIDEO_MODE_ALT_BANKS` | background and overlay alt-bank selection enabled |

`VIDEO_STATUS` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_CLIENT_CONNECTED` | a client owns the video session |
| 1 | `VIDEO_UPDATE_ACTIVE` | an accepted update is not yet acknowledged |
| 2 | `VIDEO_READOUT_ACTIVE` | MIA is reading live video RAM for send or repair |
| 3 | `VIDEO_RESPONSE_SENT` | initial response send finished; ACK may still be pending |
| 4 | `VIDEO_FULL_REFRESH_PENDING` | the next update will include all video pages |
| 5 | `VIDEO_REPAIR_ACTIVE` | repair chunks are being regenerated/sent |

`VIDEO_UPDATE_ACTIVE` is the conservative "visible memory may still affect the
client's pending update" bit. Programs that want clean output wait for this bit
to clear before changing visible memory. `VIDEO_READOUT_ACTIVE` is narrower: it
means core 0 is currently reading MIA RAM.

`LAYER_ENABLE` bits:

| Bit | Name |
| ---: | --- |
| 0 | background enabled |
| 1 | overlay enabled |
| 2 | sprites enabled |

## Graphics Model

The logical display is 320x200 pixels. Tiles are 8x8 pixels, so the visible
screen is 40x25 cells.

CHR data is planar:

```text
one character = 8 rows * 3 planes = 24 bytes
one bank      = 256 characters * 24 bytes = 6,144 bytes
eight banks   = 49,152 bytes
```

Each 3bpp character pixel selects color index `0-7` in the selected palette.
Sprites and overlay treat color index `0` as transparent. Background treats
color index `0` as visible.

The renderer has five active CHR selectors: background, background alternate,
overlay, overlay alternate, and sprite. Programs that do not use alternate
banks leave the alternate selectors equal to their primary selectors or clear
`VIDEO_MODE_ALT_BANKS`.

## Display Terms

- **Tile:** an 8x8 pixel graphic.
- **Character:** one tile definition stored in a CHR bank.
- **CHR bank:** 256 character definitions.
- **Palette bank:** 8 RGB565 colors addressed by decoded color index `0-7`.
- **Nametable:** a 40x25 grid of character indexes.
- **Attribute table:** a 40x25 grid of per-cell palette and render attributes.
- **Overlay:** a fixed screen-space nametable and attribute table for HUDs,
  menus, dialog, and debug text.
- **Sprite:** a movable 8x8 object drawn from the sprite CHR bank.
- **OAM:** Object Attribute Memory, the 256-entry sprite table.

## CHR Layout

Each CHR bank contains three 2 KiB bitplanes:

```text
bank + $0000: plane 0, low bit of every pixel
bank + $0800: plane 1, middle bit of every pixel
bank + $1000: plane 2, high bit of every pixel
```

Each plane stores 256 characters times 8 rows:

```text
plane_offset = plane * $0800
row_offset   = character * 8 + row
byte_addr    = bank_base + plane_offset + row_offset
```

Within a row byte, pixel `0` uses bit `0` and pixel `7` uses bit `7`.
The renderer reconstructs a 3-bit color index from the same row in all three
planes:

```text
color = ((plane0_row >> x) & 1)
      | (((plane1_row >> x) & 1) << 1)
      | (((plane2_row >> x) & 1) << 2)
```

## Palette Layout

There are 16 palette banks. Each bank contains 8 little-endian RGB565 colors:

```text
palette_base + palette_id * 16 + color_index * 2
```

Background pixels use all decoded color indexes as visible colors. Overlay and
sprite pixels treat decoded color `0` as transparent and use decoded colors
`1-7` as visible colors.

## Background Nametables

Each background nametable is a 40x25 grid:

```text
table_offset = table_id * 1000
cell_offset  = y * 40 + x
```

The 8 background tables are arranged as two 2x2 sets:

```text
BG_ACTIVE_SET 0:
  table 0  table 1
  table 2  table 3

BG_ACTIVE_SET 1:
  table 4  table 5
  table 6  table 7
```

`BG_SCROLL_MODE` controls how much of the active set is used:

| Mode | Plane | Tables Used |
| ---: | --- | --- |
| `0` | 40x25 cells | top-left table |
| `1` | 80x25 cells | top row |
| `2` | 40x50 cells | left column |
| `3` | 80x50 cells | full 2x2 set |

The attribute tables use the same table id and cell layout as the nametables.

## Attribute Bytes

Background and overlay attribute bytes share the same layout:

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0-3` | `PAL` | palette bank `0-15` |
| `4` | `FLIP_X` | mirror character horizontally |
| `5` | `FLIP_Y` | mirror character vertically |
| `6` | `PRIORITY` | nonzero pixels are foreground pixels above sprites |
| `7` | `CHR_ALT` | use the layer's alternate CHR bank |

For background cells, `CHR_ALT` selects between `BG_CHR_BANK` and
`BG_ALT_CHR_BANK`. For overlay cells, it selects between `OVERLAY_CHR_BANK` and
`OVERLAY_ALT_CHR_BANK`.

## Overlay

The overlay is a fixed 40x25 screen-space layer. Overlay cell `(0, 0)` always
maps to the top-left 8x8 pixels of the final 320x200 screen. Overlay rendering
ignores `SCROLL_X`, `SCROLL_Y`, `BG_ACTIVE_SET`, and `BG_SCROLL_MODE`.

Overlay color `0` is transparent. Nonzero overlay pixels are drawn with the
selected palette. Overlay priority cells also hide sprite pixels at the same
screen positions.

## OAM

OAM stores 256 sprite records. Each record is 5 bytes:

| Byte | Name | Meaning |
| ---: | --- | --- |
| `0` | `TILE` | character index in `SPRITE_CHR_BANK` |
| `1` | `X` | low 8 bits of signed screen X |
| `2` | `Y` | low 8 bits of signed screen Y |
| `3` | `ATTR` | palette and render attributes |
| `4` | `EXT` | extended coordinates and flags |

`ATTR` layout:

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0-3` | `PAL` | palette bank `0-15` |
| `4` | `PRIORITY` | sprite goes behind nonzero background pixels |
| `5` | `FLIP_X` | mirror sprite horizontally |
| `6` | `FLIP_Y` | mirror sprite vertically |
| `7` | reserved | zero |

`EXT` layout:

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0-1` | `X_HI` | bits 8-9 of signed 10-bit X |
| `2` | `Y_HI` | bit 8 of signed 9-bit Y |
| `3` | `DISABLE` | hide sprite when set |
| `4-7` | reserved | zero |

The renderer sign-extends X from 10 bits and Y from 9 bits:

```text
X range: -512 to 511
Y range: -256 to 255
```

## Render Order

The client composes one frame in this order:

1. backdrop color,
2. scrolling background,
3. sprites that are not hidden by priority,
4. foreground background/overlay priority pixels,
5. overlay pixels.

The exact implementation can optimize that order, but the visible result uses
those priority rules.

## Fast Video Indexes

MIA reserves high index IDs for common video writes. These indexes are
initialized by `VIDEO_ENABLE` and restored by reset. Programs select them with
`IDXA_SELECTOR` (`$FFE1`) or `IDXB_SELECTOR` (`$FFE5`) and then stream bytes
through the matching port.

All fast video indexes use step-on-write and wrap. Small control registers wrap
over exactly their field size, so repeated writes need no address preparation.

| Index | Name | Address Range | Length | Use |
| ---: | --- | ---: | ---: | --- |
| `$80` | `VIDX_SCROLL_X` | `$00008-$00009` | 2 | write `SCROLL_X` low, high, repeat |
| `$81` | `VIDX_SCROLL_Y` | `$0000A-$0000B` | 2 | write `SCROLL_Y` low, high, repeat |
| `$82` | `VIDX_BG_PLANE` | `$0000C-$0000D` | 2 | write `BG_ACTIVE_SET`, `BG_SCROLL_MODE`, repeat |
| `$83` | `VIDX_BANK_SELECT` | `$0000E-$00012` | 5 | write bg, bg alt, overlay, overlay alt, sprite banks |
| `$84` | `VIDX_LAYER_ENABLE` | `$00003-$00003` | 1 | write layer enable flags |
| `$85` | `VIDX_OAM_COUNT` | `$00014-$00015` | 2 | write active sprite count low, high, repeat |
| `$86` | `VIDX_FRAME_FLAGS` | `$00016-$00016` | 1 | write render frame flags |
| `$87` | `VIDX_VIDEO_STATUS` | `$00002-$00002` | 1 | read `VIDEO_STATUS` |
| `$88` | `VIDX_PALETTE` | `$00100-$001FF` | 256 | stream palette bytes |
| `$89` | `VIDX_OAM` | `$10850-$10D4F` | 1,280 | stream sprite records |
| `$8A` | `VIDX_OVERLAY_NT` | `$10080-$10467` | 1,000 | stream overlay nametable |
| `$8B` | `VIDX_OVERLAY_ATTR` | `$10468-$1084F` | 1,000 | stream overlay attributes |
| `$90-$97` | `VIDX_CHR_BANK_0-7` | bank base | 6,144 | stream one CHR bank |
| `$A0-$A7` | `VIDX_BG_NT_0-7` | table base | 1,000 | stream one background nametable |
| `$A8-$AF` | `VIDX_BG_ATTR_0-7` | table base | 1,000 | stream one background attribute table |

For example, after selecting `$80`, every two writes update `SCROLL_X` and the
index returns to `SCROLL_X` low:

```asm
lda #$80        ; VIDX_SCROLL_X
sta $FFE1       ; IDXA_SELECTOR

lda scroll_x_lo
sta $FFE0       ; SCROLL_X low
lda scroll_x_hi
sta $FFE0       ; SCROLL_X high, index wraps
```

The general-purpose index configuration path remains available for custom
ranges. Fast video indexes exist so hot per-frame fields do not need descriptor
setup.

## Dirty Tracking

MIA tracks dirty video state in 32-byte pages. A write through an indexed window
into video memory sets the matching dirty page bit in the active dirty map.
Writes outside the video state range do not create video dirty bits.

For the 68,944-byte video state range:

```text
ceil(68,944 B / 32 B) = 2,155 pages
2,155 bits = 270 bytes
```

MIA keeps two 270-byte dirty maps:

| Dirty map | Owner | Meaning |
| --- | --- | --- |
| active | core 1 write path | receives bits for new 6502 writes |
| pending | core 0 video service | retained page list for the current response |

When no response is pending, the non-active map is already clear and ready for
the next rotation. There is no separate third map and no explicit "free" flag:
with two maps, the clear non-active map is the available map by definition.

The core 1 write-side operation is bounded:

```text
MIA_RAM[offset] = value
page = offset >> 5
active_dirty[page >> 3] |= 1 << (page & 7)
```

Core 1 does not copy page data. Core 0 does the slower work: dirty-map rotation,
dirty page list construction, packet construction, repair handling, bounded send
scheduling, and UDP sending. Core 0 never clears the active map and never clears
the pending map while it is retained for repair.

## Readout Lifecycle

When the client requests an update and no response is pending, MIA performs a
small coordinated dirty-map rotation:

1. if full refresh is not pending and the active dirty map is empty, return
   `STATUS(NO_DIRTY_PAGES)`;
2. rotate the active dirty map into the pending role and the already-clear other
   map into the active role;
3. if full refresh is pending, set every valid page bit in the pending map and
   clear `VIDEO_FULL_REFRESH_PENDING`;
4. assign the next nonzero `FRAME_ID`;
5. set `VIDEO_UPDATE_ACTIVE` and `VIDEO_READOUT_ACTIVE`;
6. emit `VIDEO_EVENT_FRAME_REQUEST` and `VIDEO_EVENT_READOUT_START`.

After the rotation, new 6502 writes are tracked in the active map for the next
client update. Core 0 reads the pending dirty map, builds fixed page records in
ascending page order, and sends `FRAME_DATA` chunks.

When the first complete response has been sent, MIA clears
`VIDEO_READOUT_ACTIVE`, sets `VIDEO_RESPONSE_SENT`, and emits
`VIDEO_EVENT_FRAME_SENT` and `VIDEO_EVENT_READOUT_END`. The pending dirty map is
still retained because the client may request repair.

When the client acknowledges the response, MIA first clears the pending dirty
map. Only after that cleanup does it clear `VIDEO_UPDATE_ACTIVE` and
`VIDEO_RESPONSE_SENT`, release the pending response state, update the
acknowledged client frame id, and emit `VIDEO_EVENT_FRAME_ACKED`. This order
guarantees that a 6502 program observing the ACK event sees one active map for
future writes and one already-clear map ready for the next accepted request.

If the client requests missing chunks, MIA sets `VIDEO_REPAIR_ACTIVE` and
`VIDEO_READOUT_ACTIVE`, regenerates those chunks from the same pending dirty map
and current MIA RAM values, sends them, then clears the repair/readout bits.

## Readout Visibility Rules

The pending dirty map freezes the list of pages for a response. It does not
freeze the byte values in those pages. Core 0 reads live MIA RAM while sending
or repairing an update.

This means visible writes during `VIDEO_READOUT_ACTIVE` can appear in the update
currently being generated. Visible writes after `VIDEO_RESPONSE_SENT` but before
acknowledgement can appear in a later repair of that same update. In both cases,
the active dirty map also records the write for the next update, so the client
mirror converges.

Programmers choose the timing discipline:

- ignore the flags for lowest latency and possible visual artifacts;
- avoid visible writes while `VIDEO_READOUT_ACTIVE` is set to avoid first-send
  readout artifacts;
- avoid visible writes while `VIDEO_UPDATE_ACTIVE` is set to also avoid repair
  artifacts.

This is similar to classic video hardware where changing visible memory during
display readout can produce a transient artifact, but changing inactive or
off-screen state is safe.

## Video Events and IRQ

Video events are latched in `VIDEO_EVENT_STATUS`. Writing `1` to a bit clears
that event bit. `VIDEO_IRQ_ENABLE` selects which latched events assert the MIA
video IRQ source.

| Bit | Event | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_EVENT_FRAME_REQUEST` | MIA accepted a client update request |
| 1 | `VIDEO_EVENT_FRAME_SENT` | initial response send completed |
| 2 | `VIDEO_EVENT_FRAME_ACKED` | client acknowledged the response |
| 3 | `VIDEO_EVENT_CLIENT_CHANGE` | `VIDEO_CLIENT_CONNECTED` changed |
| 4 | `VIDEO_EVENT_FULL_REFRESH` | all pages were marked dirty |
| 5 | `VIDEO_EVENT_REPAIR_REQUEST` | client requested missing chunks |
| 6 | `VIDEO_EVENT_READOUT_START` | MIA started reading video RAM for send/repair |
| 7 | `VIDEO_EVENT_READOUT_END` | MIA finished the current readout/repair pass |

When `(VIDEO_EVENT_STATUS & VIDEO_IRQ_ENABLE) != 0`, MIA sets
`IRQ_VIDEO_EVENT` (`$0020`) in the normal `IRQ_STATUS` register. The 6502
enables delivery with the normal `IRQ_MASK` register.

## Client Pacing

The client owns the request cadence. It sends `REQUEST_FRAME` at its chosen FPS
only when the previous response is complete, acknowledged, absent, or being
explicitly retried. Repair timeout is also client-local policy.

The 6502 program chooses how tightly to synchronize its visible writes:

- free-running programs write whenever they want and tolerate artifacts;
- readout-paced programs wait for `VIDEO_READOUT_ACTIVE` to clear;
- ack-paced programs wait for `VIDEO_UPDATE_ACTIVE` to clear.

If the network is slow, ack-paced programs slow down because the client
acknowledgement arrives later. This is a programming choice, not hidden firmware
backpressure.

## Packet Size Target

The application UDP payload is 512 bytes. The 32-byte protocol header lives
inside that payload, leaving 480 bytes for response payload. A fixed page record
is 34 bytes, so every full packet carries 14 page records, or 476 response
bytes.

A full refresh has one page record for every video page:

```text
2,155 records * 34 bytes = 73,270 bytes
ceil(2,155 / 14) = 154 chunks at the fixed payload size
```

Normal updates are expected to be much smaller. Bandwidth becomes limiting when
a program frequently changes large CHR ranges, rewrites whole nametables, or
forces repeated full refreshes.

## Firmware Service Loop

The firmware links `pico_cyw43_arch_lwip_poll`, so the main loop polls Wi-Fi
explicitly:

```c
while (true) {
    mia_handle_reset_request();
    mia_service();
    cyw43_arch_poll();
    mia_video_service();
    update_onboard_led_blink();
    tight_loop_contents();
}
```

`mia_video_service()` is nonblocking and bounded per call. It sends only the
packets allowed by pbuf availability and the accepted transport budget.
