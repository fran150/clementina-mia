# MIA Video Output Capability

This document describes the planned video output capability for MIA on the
current `experimental` firmware line. It uses the older `.kiro` MIA graphics
notes from `main` as a starting point, but adapts the design to the firmware
that exists now:

- MIA appears to Clementina as a 32-byte register block at `$FFE0-$FFFF`.
- MIA owns 128 KiB of internal RAM.
- Clementina accesses MIA RAM through two CPU-facing indexed windows, `IDX A`
  and `IDX B`.
- The Pico 2 W already links the CYW43/lwIP polling Wi-Fi stack, but no video
  network service exists yet.

The central idea is not to stream pixels. MIA should expose a compact
tile/sprite graphics state, send that state and its updates over Wi-Fi, and let
the desktop client render the final pixels.

## Goals

- Provide a practical "video output" path without VGA hardware.
- Keep the 6502 programming model small and retro-friendly.
- Use MIA RAM as a PPU-like graphics memory space.
- Send graphics state updates over Wi-Fi, not raw framebuffers.
- Support a useful first target of 320x200 at 25 FPS.
- Keep 30 FPS feasible if the Wi-Fi link is healthy.
- Allow a client on a computer to connect to MIA and render Clementina's screen.
- Leave enough MIA RAM for control data, dirty tracking, and future extensions.

## Non-Goals

- MIA will not initially generate VGA, HDMI, composite, or other physical video.
- MIA will not initially stream full RGB pixel frames.
- MIA will not initially perform full raster composition on the Pico.
- The network stream is not intended to be a general video codec.
- The first protocol does not need multiple simultaneous viewers.

## Current Firmware Constraints

The current firmware shape matters because it changes the old `.kiro` plan in
several ways.

| Area | Current firmware | Impact on video design |
| - | - | - |
| CPU-visible interface | 32 registers at `$FFE0-$FFFF` | Video must be reached through the existing indexed RAM windows and commands. |
| MIA RAM | 128 KiB | The old 256 KiB memory plan must be reduced. The tile/sprite model still fits comfortably. |
| Indexed windows | `IDX A` and `IDX B` only | Two active windows are enough for streaming writes and control updates, but not the old four-window model. |
| Index descriptors | 256 descriptors | The old index allocation idea still ports well. |
| Config interface | Currently configures indexes 0 and 1 directly | Video indexes should be preconfigured by firmware first; arbitrary index configuration can come later. |
| Wi-Fi stack | `pico_cyw43_arch_lwip_poll` | Wi-Fi work must be serviced from the main loop with `cyw43_arch_poll()`. |
| Core split | Core 1 handles the time-critical bus action loop | Network and video publishing must stay off the bus fast path. |

The most important rule is that Wi-Fi must never wait inside the bus service
path. CPU writes to MIA RAM may mark tiny dirty flags, but packet construction
and transmission must be background work.

## Architecture

MIA video should be treated as a remote PPU:

```text
Clementina 6502
    writes palettes, tiles, nametables, sprites, and control bytes
    through MIA's indexed RAM windows

MIA firmware
    stores the PPU state in MIA RAM
    tracks changed regions
    publishes snapshots and updates over Wi-Fi

Video client
    keeps a mirror of the PPU state
    renders 320x200 pixels locally
    presents frames on the host computer
```

This is much more efficient than raw frame streaming. Most frames only change a
few nametable entries, sprite records, scroll registers, or palette values.
Character graphics change less often and can be synced as resources.

## Display Model

The first video mode should be a NES-like tile/sprite display:

| Property | Value | Notes |
| - | - | - |
| Logical resolution | 320x200 pixels | The image size the client renders before scaling. |
| Tile size | 8x8 pixels | The basic reusable graphics block. |
| Tile grid | 40x25 cells | `320 / 8 = 40` columns and `200 / 8 = 25` rows. |
| Tile graphics | 3 bits per pixel, 8 color indexes | Each tile pixel selects one of 8 entries from the chosen palette. For sprites, index 0 is transparent. |
| Tile size in memory | 24 bytes | `8 * 8 * 3 bits = 192 bits`. |
| Character banks | 8 banks | Groups of reusable tile graphics. A bank is selected by control state. |
| Characters per bank | 256 | Each character is one 8x8 tile definition. |
| Palette banks | 16 banks | Groups of actual RGB colors used by tiles and sprites. |
| Colors per palette | 8 | Tile pixel values `0-7` select one of these colors. |
| Color format | RGB565, little-endian | 16-bit color, suitable for compact storage and easy client rendering. |
| Nametables | 4 tables, 40x25 bytes each | Background tile maps. Each byte names which character appears in a cell. |
| Palette tables | 4 tables, 40x25 bytes each | Background palette maps. Each byte selects the palette bank for a matching nametable cell. |
| Sprites | 256 objects | Movable objects drawn over or behind the background. |
| Sprite size | 8x8 pixels for v1 | Sprites reuse character graphics. 8x16 can be added later. |
| OAM size | 256 sprites * 4 bytes = 1024 bytes | OAM stores the sprite list: position, character id, attributes, and X coordinate. |

### Display Terms

**Tile:** An 8x8 pixel graphic. Tiles are the small reusable pieces that make up
backgrounds and sprites.

**Character:** A tile definition stored in a character bank. The word
"character" comes from classic text/tile hardware, but it can represent letters,
terrain, icons, UI pieces, or sprite artwork.

**Character bank:** A collection of 256 character definitions. Having 8 banks
allows software to keep multiple graphic sets resident, such as font tiles, UI
tiles, background tiles, and sprite tiles.

**Palette bank:** A collection of 8 actual RGB565 colors. A tile pixel stores a
small number from `0` to `7`; the selected palette bank converts that number
into a real color.

**Nametable:** A background map. Each nametable cell stores a character index,
which tells the renderer which 8x8 tile to draw at that position.

**Palette table:** A color-selection map paired with a nametable. Each palette
table cell selects which palette bank to use for the matching background tile.

**Sprite:** A movable 8x8 object, such as a cursor, player, projectile, or icon.
Sprites use character graphics too, but their position and attributes come from
OAM instead of from the nametable.

**OAM:** Object Attribute Memory. This is the sprite table. Each sprite record
stores the sprite's Y position, character index, attributes, and X position.

The client composes the final image from the mirrored state. MIA only needs to
transmit state changes and frame boundaries.

## Graphics Resources

### Character Banks

Each character bank contains 256 8x8 characters. Each pixel is a 3-bit palette
index from 0 to 7.

```text
1 character = 8 * 8 * 3 bits = 192 bits = 24 bytes
1 bank      = 256 * 24 bytes = 6144 bytes
8 banks     = 49152 bytes
```

The exact bit packing should be fixed for all clients. The recommended v1
packing is row-major, little bit order inside each row:

```text
row 0 pixels 0..7
row 1 pixels 0..7
...
row 7 pixels 0..7
```

For each row, the 24 bits are packed as eight 3-bit values:

```text
byte 0: p0 bits 0..2, p1 bits 0..2, p2 bits 0..1
byte 1: p2 bit 2, p3, p4, p5 bit 0
byte 2: p5 bits 1..2, p6, p7
```

This packing is compact. If the 6502-side drawing code becomes awkward, a later
mode can add planar 2bpp or 4bpp tiles, but 3bpp matches the older design and
keeps the palette model simple.

### Palette Banks

There are 16 palette banks. Each bank has 8 RGB565 colors.

```text
1 palette bank = 8 colors * 2 bytes = 16 bytes
16 banks       = 256 bytes
```

For background tiles, each cell in the palette table selects one of these 16
palette banks. For sprites, each sprite attribute byte selects one palette bank.

Color index 0 should be treated as transparent for sprites. For background
tiles, color index 0 is a normal color.

### Nametables

Each nametable is a 40x25 grid of character indexes.

```text
1 nametable = 40 * 25 = 1000 bytes
4 tables    = 4000 bytes
```

The simplest mode uses one active nametable. The older design sends two
nametables per frame to support scrolling. This can be preserved as an optional
viewport mode:

| Mode | Meaning |
| - | - |
| `0` | Single 40x25 nametable. No extended scrolling plane. |
| `1` | Horizontal 80x25 plane using two nametables. |
| `2` | Vertical 40x50 plane using two nametables. |

The first implementation can support mode `0` only and still keep the memory
layout compatible with modes `1` and `2`.

### Palette Tables

Each palette table matches a nametable cell-for-cell:

```text
1 palette table = 40 * 25 = 1000 bytes
4 tables        = 4000 bytes
```

Each byte uses the low nibble as the palette bank id. The high nibble is
reserved for future cell attributes, such as tile flip or priority.

### OAM

Object Attribute Memory stores 256 sprite records.

```text
1 sprite = 4 bytes
256 sprites = 1024 bytes
```

Recommended v1 sprite layout:

| Byte | Name | Description |
| - | - | - |
| `0` | `Y` | Sprite top Y coordinate. |
| `1` | `TILE` | Character index within the active sprite character bank. |
| `2` | `ATTR` | Palette and render attributes. |
| `3` | `X` | Sprite left X coordinate. |

Recommended `ATTR` layout:

| Bits | Name | Description |
| - | - | - |
| `0-3` | `PAL` | Palette bank id, 0 to 15. |
| `4` | `PRIORITY` | `0` = in front of background, `1` = behind nonzero background pixels. |
| `5` | `FLIP_X` | Horizontal flip. |
| `6` | `FLIP_Y` | Vertical flip. |
| `7` | `DISABLE` | `1` hides the sprite. |

The old docs mentioned priority and flip bits, but did not assign the remaining
bit. Using bit 7 as `DISABLE` gives software an inexpensive way to hide sprites
without moving them offscreen.

## Proposed MIA RAM Layout

This layout fits inside the current 128 KiB MIA RAM region.

| Start | Size | End | Description |
| - | - | - | - |
| `0x00000` | `0x0100` | `0x000FF` | Video control block |
| `0x00100` | `0x0100` | `0x001FF` | 16 palette banks |
| `0x00200` | `0xC000` | `0x0C1FF` | 8 character banks |
| `0x0C200` | `0x0FA0` | `0x0D19F` | 4 nametables |
| `0x0D200` | `0x0FA0` | `0x0E19F` | 4 palette tables |
| `0x0E200` | `0x0400` | `0x0E5FF` | Sprite OAM |
| `0x0E600` | `0x0100` | `0x0E6FF` | Dirty flags and generation counters |
| `0x0E700` | `0x0900` | `0x0EFFF` | Reserved video expansion |
| `0x0F000` | `0x11000` | `0x1FFFF` | Free MIA RAM for other uses |

The video region consumes about 60 KiB, leaving about 68 KiB for non-video MIA
features and user data.

## Preconfigured Indexes

The older `.kiro` index plan still works well with the current 256-index
descriptor table.

| Index | Purpose | Default address | Length |
| - | - | - | - |
| `16-23` | Character banks 0-7 | `0x00200 + bank * 0x1800` | `0x1800` |
| `32-47` | Palette banks 0-15 | `0x00100 + bank * 0x10` | `0x10` |
| `48-51` | Nametables 0-3 | `0x0C200 + table * 1000` | `1000` |
| `52-55` | Palette tables 0-3 | `0x0D200 + table * 1000` | `1000` |
| `56` | Sprite OAM | `0x0E200` | `1024` |
| `57` | Video control block | `0x00000` | `256` |
| `58` | Dirty flags | `0x0E600` | `256` |
| `59-63` | Reserved video indexes | varies | varies |

Each video index should be initialized with:

```text
current_addr = default_addr
default_addr = region start
limit_addr   = region end + 1
step         = 1
flags        = W_STP_ENA | R_STP_ENA | WRAP_ENA
```

For bulk writes from the 6502, software selects one of these indexes in `IDX A`
or `IDX B`, then streams bytes through the corresponding data port.

## Video Control Block

The control block is the small register-like structure mirrored to the video
client. Multi-byte fields are little-endian.

| Offset | Size | Name | Description |
| - | - | - | - |
| `0x00` | 4 | `magic` | ASCII `MIAV`. |
| `0x04` | 1 | `version` | Video memory layout version. Start with `1`. |
| `0x05` | 1 | `mode` | `0` = disabled, `1` = tile mode 320x200. |
| `0x06` | 1 | `target_fps` | Suggested publish rate, usually 25 or 30. |
| `0x07` | 1 | `flags` | Global flags. |
| `0x08` | 2 | `frame_id` | Incremented by the 6502 when a frame is ready. |
| `0x0A` | 1 | `active_set` | `0` = tables 0/1, `1` = tables 2/3. |
| `0x0B` | 1 | `viewport_mode` | `0` single table, `1` horizontal pair, `2` vertical pair. |
| `0x0C` | 2 | `scroll_x` | Pixel scroll X for pair modes. |
| `0x0E` | 2 | `scroll_y` | Pixel scroll Y for pair modes. |
| `0x10` | 1 | `bg_chr_bank` | Character bank used for background tiles. |
| `0x11` | 1 | `sprite_chr_bank` | Character bank used for sprites. |
| `0x12` | 1 | `bg_color` | Optional backdrop palette index. |
| `0x13` | 1 | `present_flags` | Frame present and sync hints. |
| `0x14` | 2 | `dirty_mask_low` | Optional coarse dirty bits, low word. |
| `0x16` | 2 | `dirty_mask_high` | Optional coarse dirty bits, high word. |
| `0x18` | 8 | `reserved` | Reserved for timing and transport metadata. |
| `0x20` | 224 | `future` | Reserved. |

Recommended `flags` bits:

| Bit | Name | Description |
| - | - | - |
| `0` | `ENABLE_BG` | Render background. |
| `1` | `ENABLE_SPRITES` | Render sprites. |
| `2` | `REQUEST_KEYFRAME` | Client or 6502 may request a full sync. |
| `3` | `FRAME_LOCK` | 6502 is updating active state; publisher should wait. |
| `4-7` | Reserved | Must be zero for v1. |

Recommended `present_flags` bits:

| Bit | Name | Description |
| - | - | - |
| `0` | `PRESENT` | 6502 toggles or sets when a coherent frame is ready. |
| `1` | `FORCE_FULL_FRAME` | Publisher should send full active tables and OAM. |
| `2` | `FORCE_RESOURCE_SYNC` | Publisher should send all palettes and character banks. |
| `3-7` | Reserved | Must be zero for v1. |

## CPU Programming Model

From the 6502 side, video memory is just MIA RAM behind indexes.

### Writing a Palette Bank

1. Select the palette bank index in `IDXA_SELECT`.
2. Write 16 bytes to `IDXA_PORT`.
3. The preconfigured index auto-steps and wraps at the palette bank limit.

Example: palette bank 3 uses MIA index `35`.

```text
write $FFE1 = 35      ; select index 35 in window A
write $FFE0 = color0 low
write $FFE0 = color0 high
...
write $FFE0 = color7 high
```

### Writing a Character

Character bank 0 uses index `16`. Character `N` starts at:

```text
0x00200 + N * 24
```

The current firmware does not yet expose direct configuration for arbitrary
index current addresses. The first implementation should therefore add helper
commands or preconfigured sub-indexes if software needs random character
updates. The simpler first path is to stream an entire character bank at boot or
asset-load time.

### Updating a Nametable Cell

Nametable 0 uses index `48`. Cell `(x, y)` starts at:

```text
0x0C200 + y * 40 + x
```

For efficient random cell updates, the firmware should eventually expose one of
these mechanisms:

- A command to set the current address of any index.
- A command to seek a preconfigured video index by offset.
- A small video command queue in MIA RAM.

Until that exists, sequential writes are easy and random writes are awkward.

### Presenting a Frame

The 6502 should update inactive tables when double buffering is used, then
write the control block fields:

```text
active_set = next set
frame_id   = frame_id + 1
present_flags.PRESENT toggled or set
```

The network publisher treats `frame_id` changes as frame boundaries.

## Dirty Tracking

The first implementation can send a complete active frame package at every
publish interval:

```text
2 nametables     = 2000 bytes
2 palette tables = 2000 bytes
OAM              = 1024 bytes
control block    = 256 bytes
total            = about 5.2 KiB per frame
```

At 25 FPS this is about 130 KiB/s. At 30 FPS this is about 156 KiB/s. That is
well within a realistic Wi-Fi budget for Pico W-class hardware.

After the full-frame path works, add dirty tracking:

| Region | Recommended dirty granularity |
| - | - |
| Palette banks | 1 palette bank, 16 bytes |
| Character banks | 1 character, 24 bytes |
| Nametables | range of cells or 40-byte row |
| Palette tables | range of cells or 40-byte row |
| OAM | 1 sprite record, 4 bytes |
| Control block | whole block or changed range |

Dirty marking must stay cheap. If marking happens in the index write path, it
should only set a byte, bit, or generation counter. The main loop can later
expand those flags into packets.

An alternative is shadow comparison: the publisher keeps a shadow copy of video
regions and diffs them every frame. That keeps the bus path clean but costs more
background CPU time. For v1, a hybrid approach is reasonable:

- Send full active frame packages first.
- Add explicit dirty bits for palettes, OAM, and control.
- Add row-level or tile-level dirty tracking for nametables.
- Add character-level dirty tracking for character banks when dynamic character
  updates become common.

## Wi-Fi Transport

Use UDP for video data. A dropped frame update is better than blocking the 6502
or building a backlog.

The current `lwipopts.h` uses small packet buffers:

```text
PBUF_POOL_BUFSIZE = 592
TCP_MSS           = 536
```

For the first UDP protocol, keep payloads at or below 512 bytes. This avoids IP
fragmentation and fits the current buffer configuration with room for headers.

### Connection Model

The first connection model should be unicast UDP:

1. Client sends `HELLO` to MIA.
2. MIA records the client's IP and port.
3. MIA sends `WELCOME`.
4. MIA sends a full snapshot.
5. MIA starts periodic frame/update packets.

MIA can run either:

- As a station on an existing Wi-Fi network.
- As a Pico-created access point for direct laptop connection.

AP mode is attractive for demos because any computer can join the MIA network
without router configuration. STA mode is nicer for development.

### Packet Header

All multi-byte fields are little-endian.

```c
typedef struct {
    uint8_t  magic[4];       // "MIAV"
    uint8_t  version;        // 1
    uint8_t  type;           // packet type
    uint8_t  header_len;     // bytes, including extensions
    uint8_t  flags;          // packet flags
    uint32_t session_id;     // random per client session
    uint32_t frame_id;       // frame/control generation
    uint16_t sequence;       // packet sequence within session
    uint16_t chunk_index;    // chunk number for multi-packet payloads
    uint16_t chunk_count;    // total chunks for this message
    uint8_t  resource_type;  // palette, chr, nametable, etc.
    uint8_t  resource_id;    // bank/table/sprite id as applicable
    uint32_t offset;         // byte offset within the resource
    uint16_t payload_len;    // bytes after this header
    uint16_t crc16;          // optional; 0 means unused
} mia_video_packet_t;
```

Recommended packet types:

| Type | Direction | Purpose |
| - | - | - |
| `0x01` | Client to MIA | `HELLO` |
| `0x02` | MIA to client | `WELCOME` |
| `0x03` | MIA to client | `SNAPSHOT_BEGIN` |
| `0x04` | MIA to client | `RESOURCE_CHUNK` |
| `0x05` | MIA to client | `FRAME_BEGIN` |
| `0x06` | MIA to client | `FRAME_UPDATE` |
| `0x07` | MIA to client | `FRAME_END` |
| `0x08` | Client to MIA | `REQUEST_SNAPSHOT` |
| `0x09` | Client to MIA | `REQUEST_RESOURCE` |
| `0x0A` | Bidirectional | `PING` or `HEARTBEAT` |

Recommended resource types:

| Type | Resource |
| - | - |
| `0x01` | Video control block |
| `0x02` | Palette bank |
| `0x03` | Character bank |
| `0x04` | Nametable |
| `0x05` | Palette table |
| `0x06` | OAM |
| `0x07` | Dirty metadata |

### Reliability Strategy

UDP needs a simple recovery model.

Critical state:

- Palette banks
- Character banks
- Full snapshots
- Control block layout/version changes

Opportunistic state:

- Per-frame OAM updates
- Per-frame nametable updates
- Frame boundary packets

Recommended v1 behavior:

- Client requests a full snapshot when it detects missing snapshot chunks.
- Client requests individual resources when critical chunks are missing.
- MIA sends periodic keyframes, such as every 1 to 5 seconds.
- MIA may ignore old frame updates if a newer `frame_id` has arrived.
- Client renders the newest coherent state it has.

This avoids TCP head-of-line blocking while still allowing the client to recover
from packet loss.

## Publisher Timing

The publisher should live in the main loop or another non-bus-critical service
path.

Recommended service order:

```text
main loop:
    mia_handle_reset_request()
    mia_service()
    cyw43_arch_poll()
    mia_video_service()
    update_onboard_led_blink()
```

`mia_video_service()` should:

1. Return immediately if Wi-Fi is not initialized or no client is connected.
2. Return immediately if the next frame interval has not elapsed.
3. Read the video control block.
4. If a snapshot is pending, send snapshot chunks over several service calls.
5. Otherwise send dirty updates or the full active frame package.
6. Never busy-wait for network buffers.
7. Never touch the PIO bus fast path.

## Client Renderer

The client keeps a local mirror:

```text
control block
16 palette banks
8 character banks
4 nametables
4 palette tables
OAM
```

Rendering steps:

1. Clear the output to the backdrop color.
2. Render background tiles if `ENABLE_BG` is set.
3. Render sprites with priority and transparency if `ENABLE_SPRITES` is set.
4. Present the final 320x200 image, usually scaled to a larger window.

The client can be written in Go, TypeScript, Python, C, or any language with UDP
and a simple pixel surface. The Go emulator repo is a natural place for the
first client because it already models Clementina and can share data structures.

## Bandwidth Estimates

Full active frame package:

| Data | Bytes |
| - | - |
| Two nametables | 2000 |
| Two palette tables | 2000 |
| OAM | 1024 |
| Control block | 256 |
| Total payload | 5280 |

Approximate sustained payload:

| FPS | Payload/sec |
| - | - |
| 25 | 132000 bytes/sec |
| 30 | 158400 bytes/sec |

Full resource snapshot:

| Data | Bytes |
| - | - |
| Character banks | 49152 |
| Palette banks | 256 |
| Nametables | 4000 |
| Palette tables | 4000 |
| OAM | 1024 |
| Control block | 256 |
| Total payload | 58688 |

Even a full snapshot is only about 57 KiB. Sending that on connect or as an
occasional keyframe is reasonable. Sending all character data every frame is not
necessary and should be avoided.

## Firmware Work Plan

### Phase 1: Static Video Memory Layout

- Add `video` constants for the memory map and index ids.
- Preconfigure indexes 16-23, 32-47, 48-51, 52-55, 56, 57, and 58.
- Initialize the video control block with `MIAV`, version, mode, and default FPS.
- Add tests or debug routines that prove the regions fit in 128 KiB.

### Phase 2: Local Client and Snapshot Format

- Implement the packet structs in shared documentation and client code.
- Build a desktop client that can render from a captured snapshot.
- Add a firmware debug path to send a full snapshot over UDP.
- Keep packet payloads <= 512 bytes.

### Phase 3: Live Wi-Fi Stream

- Initialize Wi-Fi in AP or STA mode.
- Add `cyw43_arch_poll()` to the main loop.
- Add UDP `HELLO`/`WELCOME`.
- Send a full snapshot on connection.
- Send full active frame packages at 25 FPS.

### Phase 4: Dirty Updates

- Add dirty flags/generation counters for palettes, control, OAM, and tables.
- Add resource update packets.
- Add periodic keyframes.
- Add client-side resource requests for missed critical chunks.

### Phase 5: Richer PPU Behavior

- Add pair-mode scrolling.
- Add sprite priority rules and optional collision status.
- Add 8x16 sprite mode if needed.
- Add optional compression for long repeated table regions.

## Open Questions

- Should v1 target 25 FPS, 30 FPS, or allow both through the control block?
- Should MIA default to AP mode for easy client connection, or STA mode for home
  network development?
- Should character data stay 3bpp packed, or should we add a simpler planar
  format for easier 6502 authoring?
- Should the 6502 signal frame readiness by changing `frame_id`, toggling a
  `PRESENT` bit, or issuing a future command?
- Should arbitrary video index seeking be added as commands, config fields, or a
  small video command queue?
- Should the first client live in the Go emulator repo or as a small standalone
  viewer in this firmware repo?

## Summary

The feasible path is a remote PPU, not a remote framebuffer. MIA stores compact
tile, palette, nametable, and sprite state in its 128 KiB RAM. Clementina writes
that state through the existing indexed memory interface. MIA publishes state
snapshots and updates over UDP. The client renders locally.

This keeps bandwidth low, fits the current memory budget, respects the
time-critical bus architecture, and gives Clementina a video model that feels
like a period-appropriate graphics chip rather than a pixel streaming device.
