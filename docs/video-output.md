# MIA Video Output

This document defines MIA's Wi-Fi video-output architecture. The wire protocol
is defined in [video-protocol.md](video-protocol.md), and the 6502 programming
model is defined in [video-programmer-guide.md](video-programmer-guide.md).

MIA video uses a client-paced update model:

- The client keeps a complete mirror of MIA's syncable render state.
- MIA divides the 68,944-byte video region into 2,155 pages of 32 bytes each.
- Page 0 is local MIA/6502 control state and is never marked dirty or sent.
- MIA tracks the 2,154 syncable pages with a 270-byte dirty map, where each
  syncable page keeps its absolute page bit and bit 0 is reserved.
- The client requests updates at the pace its display and network can sustain.
- MIA keeps two dirty maps: one active map for new 6502 writes, and one pending
  map for the update currently being generated or repaired.
- When a request is accepted, MIA moves the active map into the pending role,
  makes the already-clear other map active for new writes, and sends the pages
  named by the pending map.
- While MIA is generating or repairing an update, it reads live MIA RAM.
- The 6502 program can use status bits and IRQ events to avoid changing visible
  memory while an update is outstanding.

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

The video region occupies the first 68,944 bytes of MIA RAM. All offsets are
byte offsets from the start of MIA RAM.

| Offset | Size | Region | Purpose |
| ---: | ---: | --- | --- |
| `$00000` | 32 B | `LOCAL_CONTROL` | MIA/6502 local control, counters, and diagnostics; never sent to the client |
| `$00020` | 32 B | `RENDER_CONTROL` | mode, scroll, layer enables, active banks, and CHR decode controls |
| `$00040` | 192 B | `CONTROL_RESERVED` | reserved; sent as zero if included in a full refresh |
| `$00100` | 256 B | `PALETTE` | 16 palette banks, 8 RGB565 colors each |
| `$00200` | 49,152 B | `CHR` | 8 banks, 256 characters per bank, 24 bytes per character |
| `$0C200` | 8,000 B | `BG_NT` | 8 background nametables, 40x25 bytes each |
| `$0E140` | 8,000 B | `BG_ATTR` | 8 background attribute tables, 40x25 bytes each |
| `$10080` | 1,000 B | `OV_NT` | fixed screen-space overlay nametable |
| `$10468` | 1,000 B | `OV_ATTR` | fixed screen-space overlay attributes |
| `$10850` | 1,280 B | `OAM` | 256 sprite records, 5 bytes each |
| `$10D50` | - | end | first byte after video state |

The syncable client mirror uses absolute MIA video offsets from `$00020` through
`$10D4F`. A full refresh sends all 2,154 syncable pages and is appropriate for
startup and recovery. Normal updates are expected to dirty a much smaller
subset of the mirror.

## Local Control Page

Page 0 (`$00000-$0001F`) is local to MIA and the 6502 program. It is never
marked dirty and is never sent in `FRAME_DATA`. The page is little-endian.
Fields marked read-only are written by MIA and read by the 6502 program.
Local counters and diagnostic fields belong here; fields sampled by the client
renderer belong in the syncable render control page. Video lifecycle level bits
live in the general `MIA_STATUS` register, and video lifecycle events use the
normal `IRQ_STATUS`/`IRQ_MASK` registers.

| Offset | Size | Field | Access | Meaning |
| ---: | ---: | --- | --- | --- |
| `$00` | 1 | `VIDEO_VERSION` | read-only | video state layout version |
| `$01` | 3 | reserved | - | zero |
| `$04` | 4 | `FRAME_ID` | read-only | latest assigned update frame id |
| `$08` | 2 | `LAST_RESPONSE_DIRTY_PAGES` | read-only | dirty page count for the latest stable update result |
| `$0A` | 22 | reserved | - | zero |

`FRAME_ID` starts at `0` for a new session and increments when MIA accepts a
client update request that produces `FRAME_DATA`. It wraps from `0xFFFFFFFF` to
`1`; `0` is reserved for "no update applied yet."

`LAST_RESPONSE_DIRTY_PAGES` is updated only at a stable point. For a
`FRAME_DATA` response, MIA updates it after the first complete response send has
finished and before setting `MIA_STAT_VIDEO_FRAME_SENT` and
`IRQ_VIDEO_FRAME_SENT`. Until then, it still contains the previous stable value.
If a request produces `STATUS(NO_DIRTY_PAGES)`, MIA writes `0` when returning
that status because no response is being built. A full refresh reports `2,154`.
Programs can use this field with the video lifecycle flags or events to detect
large updates and decide whether to slow visible writes until the client catches
up.

## Render Control Page

Page 1 (`$00020-$0003F`) is syncable render state. The client mirrors this page
and uses it while rendering.

| Offset | Size | Field | Access | Meaning |
| ---: | ---: | --- | --- | --- |
| `$20` | 1 | `VIDEO_MODE` | read/write | video enable and renderer mode bits |
| `$21` | 1 | `LAYER_ENABLE` | read/write | background, overlay, and sprite enables |
| `$22` | 1 | `BG_VIEWPORT_MODE` | read/write | selects viewport mapping within the active set; see Background Nametables |
| `$23` | 1 | `BG_ACTIVE_SET` | read/write | selects background set 0 or 1; see Background Nametables |
| `$24` | 2 | `SCROLL_X` | read/write | viewport X position in the active background plane |
| `$26` | 2 | `SCROLL_Y` | read/write | viewport Y position in the active background plane |
| `$28` | 1 | `BG_CHR_BANK` | read/write | primary background CHR bank `0-7` |
| `$29` | 1 | `BG_ALT_CHR_BANK` | read/write | alternate background CHR bank `0-7` |
| `$2A` | 1 | `OVERLAY_CHR_BANK` | read/write | primary overlay CHR bank `0-7` |
| `$2B` | 1 | `OVERLAY_ALT_CHR_BANK` | read/write | alternate overlay CHR bank `0-7` |
| `$2C` | 1 | `SPRITE_CHR_BANK` | read/write | sprite CHR bank `0-7` |
| `$2D` | 1 | `CHR_1BPP_MASK` | read/write | CHR banks decoded as 1bpp instead of 3bpp |
| `$2E` | 1 | `CHR_1BPP_PLANES` | read/write | 1bpp plane selectors for background, sprite, and overlay |
| `$2F` | 1 | `BACKDROP_COLOR` | read/write | palette bank and color index used for the backdrop |
| `$30` | 1 | `OAM_LAST_INDEX` | read/write | last OAM record index evaluated when sprites are enabled |
| `$31` | 15 | reserved | - | zero |

`VIDEO_MODE` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_MODE_ENABLE` | video service enabled |
| 1-7 | reserved | zero |

`LAYER_ENABLE` bits:

| Bit | Name |
| ---: | --- |
| 0 | background enabled |
| 1 | overlay enabled |
| 2 | sprites enabled |

`CHR_1BPP_MASK` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| `0-7` | `CHR_BANK_N_1BPP` | decode CHR bank N as 1bpp; clear means normal 3bpp |

`CHR_1BPP_PLANES` selects which character plane to use when a layer's selected
CHR bank is marked 1bpp:

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0-1` | `BG_PLANE` | background plane selector `0-2`; `3` reserved |
| `2-3` | `SPRITE_PLANE` | sprite plane selector `0-2`; `3` reserved |
| `4-5` | `OVERLAY_PLANE` | overlay plane selector `0-2`; `3` reserved |
| `6-7` | reserved | zero |

`BACKDROP_COLOR` selects one color from the palette memory:

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0-2` | `COLOR` | color index `0-7` within the selected palette bank |
| `3-6` | `PALETTE` | palette bank `0-15` |
| `7` | reserved | zero |

`OAM_LAST_INDEX` is a render limit, not a per-sprite enable flag. When the
sprite layer is enabled in `LAYER_ENABLE`, the client evaluates OAM records `0`
through `OAM_LAST_INDEX`, inclusive. For example, `0` evaluates only record 0,
and `255` evaluates all 256 records. To render no sprites, clear the sprite bit
in `LAYER_ENABLE`. Records after `OAM_LAST_INDEX` are ignored even if their
`DISABLE` bit is clear. Sprites outside this range are not drawn even if their
OAM record would otherwise mark them as visible. The goal is to avoid the
renderer having to evaluate all 256 sprites for each frame if the programmer
is not using all of them.

## Graphics Model

The logical display is 320x200 pixels. Tiles are 8x8 pixels, so the visible
screen is 40x25 cells.

CHR data is planar:

```text
one character = 8 rows * 3 planes = 24 bytes
one bank      = 256 characters * 24 bytes = 6,144 bytes
eight banks   = 49,152 bytes
```

Each 3bpp character pixel selects color index `0-7` in the selected palette. If
the selected CHR bank is marked in `CHR_1BPP_MASK`, the renderer reads only the
layer's selected plane and decodes color index `0` or `1`. Sprites and overlay
treat color index `0` as transparent. Background treats color index `0` as
visible.

The renderer has five active CHR selectors: background, background alternate,
overlay, overlay alternate, and sprite. Programs that do not use alternate
banks leave the alternate selectors equal to their primary selectors or keep
the `CHR_ALT` bit clear in the background and overlay attribute tables.

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

The renderer reconstructs a 3-bit color index from the same row in all three
planes:

```text
color = ((plane0_row >> x) & 1)
      | (((plane1_row >> x) & 1) << 1)
      | (((plane2_row >> x) & 1) << 2)
```

When a CHR bank is marked 1bpp, those same three planes are interpreted as
three independent monochrome 256-character tables. The selected plane comes
from `CHR_1BPP_PLANES` according to the layer being rendered:

```text
color = (selected_plane_row >> x) & 1
```

This lets a single CHR bank hold three separate 1bpp fonts, icon sets, sprite
masks, or other monochrome tile sets.

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

`BG_ACTIVE_SET` selects which 2x2 set the background renderer uses. Set `0`
uses background tables `0-3`; set `1` uses tables `4-7`. The inactive set is
not rendered, so software can use it as staging space for a future room, menu,
page flip, or transition.

`BG_VIEWPORT_MODE` controls how the 320x200 viewport maps into the active
background set. Mode `0` is a single-table viewport. Modes `1-2` use two-table
planes for programs that only want to maintain two scrolling tables. Modes
`3-4` use all four tables as a long horizontal or vertical plane. Mode `5` uses
all four tables as a 2x2 plane.

In the diagrams below, `T0-T3` mean the four tables in the selected active set.
For `BG_ACTIVE_SET = 0`, they are absolute tables `0-3`. For
`BG_ACTIVE_SET = 1`, they are absolute tables `4-7`.

Each mode arranges the selected set like this:

```text
Mode 0:
T0

Mode 1:
T0 T1

Mode 2:
T0
T2

Mode 3:
T0 T1 T2 T3

Mode 4:
T0
T1
T2
T3

Mode 5:
T0 T1
T2 T3
```

| Mode | Plane | Tables Used |
| ---: | --- | --- |
| `0` | 40x25 cells | single viewport using the top-left table |
| `1` | 80x25 cells | horizontal two-table plane: `T0 T1` |
| `2` | 40x50 cells | vertical two-table plane: `T0` above `T2` |
| `3` | 160x25 cells | horizontal four-table plane: `T0 T1 T2 T3` |
| `4` | 40x100 cells | vertical four-table plane: `T0`, `T1`, `T2`, `T3` |
| `5` | 80x50 cells | four-way 2x2 plane: `T0 T1` above `T2 T3` |

Table references are relative to the active set. For active set `0`, `T0-T3`
mean background tables `0-3`; for active set `1`, they mean tables `4-7`.
Viewport coordinates wrap within the selected plane before table lookup:

| Mode | Wrap Rule |
| ---: | --- |
| `0` | X wraps every 40 cells; Y wraps every 25 cells inside `T0` |
| `1` | X wraps across `T0 -> T1 -> T0`; Y wraps every 25 cells |
| `2` | Y wraps across `T0 -> T2 -> T0`; X wraps every 40 cells |
| `3` | X wraps across `T0 -> T1 -> T2 -> T3 -> T0`; Y wraps every 25 cells |
| `4` | Y wraps across `T0 -> T1 -> T2 -> T3 -> T0`; X wraps every 40 cells |
| `5` | X wraps `T0 -> T1 -> T0` on the top row and `T2 -> T3 -> T2` on the bottom row; Y wraps top row `<->` bottom row |

In mode `1`, if the viewport starts halfway through `T1`, the right side of the
screen wraps and is read from `T0`. In mode `2`, if the viewport starts halfway
through `T2`, the lower part of the screen wraps and is read from `T0`.

`SCROLL_X` and `SCROLL_Y` are unsigned pixel coordinates of the viewport's
top-left corner within that plane. In horizontal mode, for example, `SCROLL_X`
selects both the nametable column and the pixel within that column that appears
at the left edge of the screen. The renderer derives coarse tile and fine pixel
offsets directly:

```text
coarse_col = SCROLL_X >> 3
fine_x     = SCROLL_X & 7
coarse_row = SCROLL_Y >> 3
fine_y     = SCROLL_Y & 7
```

The attribute tables use the same table id, active-set selection, viewport mode,
and cell layout as the nametables.

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
ignores `SCROLL_X`, `SCROLL_Y`, `BG_ACTIVE_SET`, and `BG_VIEWPORT_MODE`.

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

If the sprite layer is enabled, the renderer evaluates OAM records `0` through
`OAM_LAST_INDEX`, inclusive. Records outside that range are not rendered and are
not tested for visibility. Set the sprite layer bit in `LAYER_ENABLE` to `0` to
skip OAM evaluation entirely.

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

## Video Indexes

MIA RAM is normally accessed through indexed windows. The 6502 selects an index
descriptor with `IDXA_SELECT` (`$FFE1`) or `IDXB_SELECT` (`$FFE5`), then reads
or writes bytes through the matching data port, `IDXA_PORT` (`$FFE0`) or
`IDXB_PORT` (`$FFE4`). Each descriptor supplies the RAM address, length, step,
wrap behavior, and optional wrap IRQ behavior for that window.

Video indexes are preconfigured descriptors for common video access. They are
initialized by `VIDEO_ENABLE` and restored by reset. All video indexes below
use forward step-on-read, forward step-on-write, and wrap. Small control
registers wrap over exactly their field size, so repeated reads or writes need
no address preparation.

| Index | Name | Address Range | Length | Use |
| ---: | --- | ---: | ---: | --- |
| `$70` | `VIDX_LOCAL_CONTROL` | `$00000-$0001F` | 32 | read local control page; never syncs |
| `$71` | `VIDX_FRAME_ID` | `$00004-$00007` | 4 | read latest frame id |
| `$72` | `VIDX_LAST_RESPONSE_DIRTY_PAGES` | `$00008-$00009` | 2 | read latest stable dirty-page count |
| `$73-$7F` | reserved local-control indexes | - | - | reserved |
| `$80` | `VIDX_RENDER_CONTROL` | `$00020-$0003F` | 32 | stream render control page |
| `$81` | `VIDX_LAYER_ENABLE` | `$00021-$00021` | 1 | write layer enable flags |
| `$82` | `VIDX_BG_VIEWPORT` | `$00022-$00023` | 2 | write `BG_VIEWPORT_MODE`, `BG_ACTIVE_SET`, repeat |
| `$83` | `VIDX_SCROLL_X` | `$00024-$00025` | 2 | write viewport X low, high, repeat |
| `$84` | `VIDX_SCROLL_Y` | `$00026-$00027` | 2 | write viewport Y low, high, repeat |
| `$85` | `VIDX_BANK_SELECT` | `$00028-$0002C` | 5 | write bg, bg alt, overlay, overlay alt, sprite banks |
| `$86` | `VIDX_CHR_1BPP` | `$0002D-$0002E` | 2 | write `CHR_1BPP_MASK`, `CHR_1BPP_PLANES`, repeat |
| `$87` | `VIDX_BACKDROP_COLOR` | `$0002F-$0002F` | 1 | write backdrop color selector |
| `$88` | `VIDX_OAM_LAST` | `$00030-$00030` | 1 | write last evaluated OAM index |
| `$89-$8F` | reserved render-control indexes | - | - | reserved |
| `$90-$9F` | `VIDX_PALETTE_0-15` | `$00100 + n * $10` | 16 | stream one palette bank |
| `$A0-$A7` | `VIDX_CHR_BANK_0-7` | bank base | 6,144 | stream one CHR bank |
| `$A8-$AF` | `VIDX_BG_NT_0-7` | table base | 1,000 | stream one background nametable |
| `$B0-$B7` | `VIDX_BG_ATTR_0-7` | table base | 1,000 | stream one background attribute table |
| `$B8` | `VIDX_OVERLAY_NT` | `$10080-$10467` | 1,000 | stream overlay nametable |
| `$B9` | `VIDX_OVERLAY_ATTR` | `$10468-$1084F` | 1,000 | stream overlay attributes |
| `$BA-$BF` | reserved video indexes | - | - | reserved |
| `$C0-$DF` | `VIDX_OAM_SPRITE_0-31` | `$10850 + n * 5` | 5 | stream one OAM sprite record |
| `$E0-$FF` | reserved video indexes | - | - | reserved |

The default OAM sprite indexes cover the first 32 sprite records. Programs that
need bulk OAM writes or records beyond 31 can use the general-purpose index
configuration path.

For example, after selecting `$83`, every two writes update `SCROLL_X` and the
index returns to `SCROLL_X` low:

```asm
lda #$83        ; VIDX_SCROLL_X
sta $FFE1       ; IDXA_SELECT

lda scroll_x_lo
sta $FFE0       ; SCROLL_X low
lda scroll_x_hi
sta $FFE0       ; SCROLL_X high, index wraps
```

The general-purpose index configuration path remains available for custom
ranges. Default video indexes exist so hot per-frame fields do not need
descriptor setup.

## Dirty Tracking

MIA tracks dirty video state in 32-byte pages. A write through an indexed window
into video memory sets the matching dirty page bit in the active dirty map.
Writes to page 0 or outside the video state range do not create video dirty
bits.

For the 68,944-byte video state range:

```text
ceil(68,944 B / 32 B) = 2,155 pages
2,154 syncable pages, using absolute page indexes 1-2,154
2,155 absolute page bits = 270 bytes, with bit 0 reserved
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
if (offset < 32 || offset >= 68,944) return
page = offset >> 5
active_dirty[page >> 3] |= 1 << (page & 7)
```

Core 1 does not copy page data. Core 0 does the slower work: dirty-map rotation,
dirty page list construction, packet construction, repair handling, bounded send
scheduling, and UDP sending. Core 0 never clears the active map and never clears
the pending map while it is retained for repair.

## Update Lifecycle

When the client requests an update and no response is pending, MIA performs a
small coordinated dirty-map rotation:

1. if the active dirty map has no syncable page bits set, return
   `STATUS(NO_DIRTY_PAGES)`;
2. rotate the active dirty map into the pending role and the already-clear other
   map into the active role;
3. scan the pending dirty map into a page list;
4. assign the next nonzero `FRAME_ID`;
5. set `MIA_STAT_VIDEO_FRAME_REQUESTED`;
6. set `IRQ_VIDEO_FRAME_REQUEST` in `IRQ_STATUS`.

After the rotation, new 6502 writes are tracked in the active map for the next
client update. Core 0 reads the pending dirty map, builds fixed page records in
ascending page order, and sends `FRAME_DATA` chunks.

If the request returns `STATUS(NO_DIRTY_PAGES)`, MIA writes `0` to
`LAST_RESPONSE_DIRTY_PAGES` and does not assign a new `FRAME_ID`.

When the first complete response has been sent, MIA updates
`LAST_RESPONSE_DIRTY_PAGES` from the sent page count, sets
`MIA_STAT_VIDEO_FRAME_SENT`, and sets `IRQ_VIDEO_FRAME_SENT` in `IRQ_STATUS`.
The pending dirty map is still retained because the client may request repair.

When the client acknowledges the response, MIA first clears the pending dirty
map. Only after that cleanup does it clear `MIA_STAT_VIDEO_FRAME_REQUESTED` and
`MIA_STAT_VIDEO_FRAME_SENT`, release the pending response state, update the
acknowledged client frame id, and set `IRQ_VIDEO_FRAME_ACKED` in `IRQ_STATUS`.
This order
guarantees that a 6502 program observing the ACK event sees all lifecycle status
bits clear, one active map for future writes, and one already-clear map ready
for the next accepted request.

If the client requests missing chunks, MIA regenerates those chunks from the
same pending dirty map and current MIA RAM values. Repair does not change
the `MIA_STATUS` lifecycle bits; the update remains in the requested/sent
lifecycle until ACK.

## Visibility Rules

The pending dirty map freezes the list of pages for a response. It does not
freeze the byte values in those pages. Core 0 reads live MIA RAM while sending
or repairing an update.

This means visible writes while `MIA_STAT_VIDEO_FRAME_REQUESTED` is set can
appear in the update currently being generated. Visible writes after
`MIA_STAT_VIDEO_FRAME_SENT` is set but before acknowledgement can appear in a
later repair of that same update. In both cases, the active dirty map also
records the write for the next update, so the client mirror converges.

Programmers choose the timing discipline:

- ignore the flags for lowest latency and possible visual artifacts;
- avoid visible writes while either `MIA_STATUS` video lifecycle bit is set for
  clean output.

This is similar to classic video hardware where changing visible memory while it
is being sampled can produce a transient artifact, but changing inactive or
off-screen state is safe.

## Video Events and IRQ

Video lifecycle events are ordinary pending bits in the general `IRQ_STATUS`
register. `IRQ_MASK` selects which pending events drive the physical IRQ line.
Programs clear video event bits through the same `IRQ_STATUS` mechanism used for
other MIA IRQ sources.

| Bit | Event | Meaning |
| ---: | --- | --- |
| 5 | `IRQ_VIDEO_FRAME_REQUEST` | MIA accepted a client update request |
| 6 | `IRQ_VIDEO_FRAME_SENT` | initial response send completed |
| 7 | `IRQ_VIDEO_FRAME_ACKED` | client acknowledged the response |

MIA sets these bits with the normal IRQ helper. If the corresponding bit is set
in `IRQ_MASK`, MIA also sets `IRQ_TRIGGERED` and drives `IRQB` low.

## Client Pacing

The client owns the request cadence. It sends `REQUEST_FRAME` at its chosen FPS
only when the previous response is complete, acknowledged, absent, or being
explicitly retried. Repair timeout is also client-local policy.

The 6502 program chooses how tightly to synchronize its visible writes:

- free-running programs write whenever they want and tolerate artifacts;
- ack-paced programs wait for the `MIA_STATUS` video lifecycle bits to clear.

If the network is slow, ack-paced programs slow down because the client
acknowledgement arrives later. This is a programming choice, not hidden firmware
backpressure.

## Packet Size Target

The application UDP payload is 512 bytes. The 32-byte protocol header lives
inside that payload, leaving 480 bytes for response payload. A fixed page record
is 34 bytes, so every full packet carries 14 page records, or 476 response
bytes.

A full refresh has one page record for every syncable video page:

```text
2,154 records * 34 bytes = 73,236 bytes
ceil(2,154 / 14) = 154 chunks at the fixed payload size
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
