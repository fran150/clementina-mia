# MIA Video Output Capability

This document describes the planned video output capability for MIA on the
current `experimental` firmware line. The design assumes:

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

The current firmware shape sets the first implementation boundaries.

| Area | Current firmware | Impact on video design |
| - | - | - |
| CPU-visible interface | 32 registers at `$FFE0-$FFFF` | Video must be reached through the existing indexed RAM windows and commands. |
| MIA RAM | 128 KiB | The tile/sprite model fits comfortably while leaving room for other MIA features. |
| Indexed windows | `IDX A` and `IDX B` only | Two active windows are enough for streaming writes and control updates. |
| Index descriptors | 256 descriptors | Video can reserve a stable range of preconfigured indexes. |
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

The first video mode should be a tile/sprite display:

| Property | Value | Notes |
| - | - | - |
| Logical resolution | 320x200 pixels | The image size the client renders before scaling. |
| Tile size | 8x8 pixels | The basic reusable graphics block. |
| Tile grid | 40x25 cells | `320 / 8 = 40` columns and `200 / 8 = 25` rows. |
| Tile graphics | 3 bits per pixel by default, optional 1 bit per pixel per bank | Each decoded tile pixel selects a color index from the chosen palette. Transparency depends on the layer. |
| Tile size in memory | 24 bytes | Three 8-byte bitplanes per 8x8 character. In 1bpp mode, each plane acts as an independent monochrome character table. |
| Character banks | 8 banks | Groups of reusable tile graphics. A bank is selected by control state and can be flagged as 3bpp or 1bpp. |
| Characters per bank | 256 | Each character is one 8x8 tile definition. |
| Palette banks | 16 banks | Groups of actual RGB colors used by background, overlay, and sprites. |
| Colors per palette | 8 | Tile pixel values `0-7` select one of these colors. |
| Color format | RGB565, little-endian | 16-bit color, suitable for compact storage and easy client rendering. |
| Nametables | 8 background tables, 40x25 bytes each | Background tile maps. Two 2x2 sets support four-way scrolling plus optional staging or page flips. |
| Attribute tables | 8 background tables, 40x25 bytes each | Background cell attribute maps: palette, flip, priority, and alternate character bank selection. |
| Fixed overlay | 1 nametable and 1 attribute table, 40x25 bytes each | Screen-space HUD/menu layer. It uses the same cell format as the background but ignores scroll. |
| Sprites | 256 objects | Movable objects drawn over or behind the background. |
| Sprite size | 8x8 pixels for v1 | Sprites reuse character graphics. 8x16 can be added later. |
| OAM size | 256 sprites * 5 bytes = 1280 bytes | OAM stores the sprite list: tile id, position, attributes, and extended coordinates. |

### Display Terms

- **Tile:** An 8x8 pixel graphic. Tiles are the small reusable pieces that make up
backgrounds and sprites.

- **Character:** A tile definition stored in a character bank. The word
"character" comes from classic text/tile hardware, but it can represent letters,
terrain, icons, UI pieces, or sprite artwork.

- **Character bank:** A collection of 256 character definitions. Having 8 banks
allows software to keep multiple graphic sets resident, such as font tiles, UI
tiles, background tiles, and sprite tiles.

- **Character plane:** One bitplane inside a character bank. In 3bpp mode,
plane 0 stores the low bit for every pixel, plane 1 stores the middle bit, and
plane 2 stores the high bit. In 1bpp mode, each plane is an independent
monochrome character table.

- **Palette bank:** A collection of 8 actual RGB565 colors. A tile pixel stores a
small number from `0` to `7`; the selected palette bank converts that number
into a real color.

- **Nametable:** A background map. Each nametable cell stores a character index,
which tells the renderer which 8x8 tile to draw at that position.

- **Attribute table:** A cell-attribute map paired with a nametable. Each
attribute table cell selects the palette bank and render attributes for the
matching background or overlay tile.

- **Overlay:** A fixed screen-space nametable and attribute table used for HUDs,
scores, menus, dialog boxes, and other elements that should not move with the
scrolling viewport.

- **Sprite:** A movable 8x8 object, such as a cursor, player, projectile, or icon.
Sprites use character graphics too, but their position and attributes come from
OAM instead of from the nametable.

- **OAM:** Object Attribute Memory. This is the sprite table. Each sprite record
stores the sprite's character index, X/Y position, attributes, and extended
coordinate bits.

The client composes the final image from the mirrored state. MIA only needs to
transmit state changes and frame boundaries.

## Graphics Resources

### Character Banks

Each character bank contains 256 8x8 characters. A bank normally decodes each
pixel as a 3-bit palette index from 0 to 7. A bank can also be flagged as 1bpp,
where each plane acts as an independent monochrome character table.

```text
1 character = 8 * 8 * 3 bits = 192 bits = 24 bytes
1 bank      = 256 * 24 bytes = 6144 bytes
8 banks     = 49152 bytes
```

Character data is planar within each bank. A bank contains three planes:

```text
bank + 0x0000: plane 0, low bit of every pixel
bank + 0x0800: plane 1, middle bit of every pixel
bank + 0x1000: plane 2, high bit of every pixel
```

Each plane is 2048 bytes:

```text
256 characters * 8 rows = 2048 bytes
```

Within a plane, character rows are stored sequentially. Character `N`, row `Y`,
and plane `P` are addressed as:

```text
bank_base + P * 0x0800 + N * 8 + Y
```

Each row byte uses little bit order: pixel `0` is bit `0`, and pixel `7` is bit
`7`. A client reconstructs the 3-bit color index for pixel `X` by reading the
matching row byte from all three planes:

```text
color = ((plane0_row >> X) & 1)
      | (((plane1_row >> X) & 1) << 1)
      | (((plane2_row >> X) & 1) << 2)
```

This layout uses 24 bytes per character and keeps single-pixel updates simple:
changing one pixel means setting or clearing the same bit position in three row
bytes.

If a bank is flagged as 1bpp through `chr_1bpp_mask`, the renderer reads only
one selected plane for that layer:

```text
color = (selected_plane_row >> X) & 1
```

That gives each flagged bank three independent 256-character monochrome tile
sets. The selected plane comes from `chr_1bpp_planes`. A decoded 1bpp pixel
always produces color index `0` or `1`; the layer decides whether color index
`0` is visible or transparent.

### Palette Banks

There are 16 palette banks. Each bank has 8 RGB565 colors.

```text
1 palette bank = 8 colors * 2 bytes = 16 bytes
16 banks       = 256 bytes
```

For background and overlay tiles, each cell in the attribute table selects one
of these 16 palette banks and controls per-cell tile attributes. For sprites,
each sprite attribute byte selects one palette bank.

Color index `0` is a normal color for the scrolling background. Color index `0`
is transparent for sprites and overlay tiles. This rule applies after character
pixels have been decoded, whether the selected character bank is 3bpp or 1bpp:

| Layer | Decoded color `0` | Decoded color `1` |
| - | - | - |
| Background | Palette color `0` | Palette color `1` |
| Sprite | Transparent | Palette color `1` |
| Overlay | Transparent | Palette color `1` |

### Nametables

Each nametable is a 40x25 grid of character indexes.

```text
1 nametable = 40 * 25 = 1000 bytes
4 tables    = 4000 bytes per scrolling set
8 tables    = 8000 bytes total
```

Nametables are arranged as two 2x2 sets. `active_set` selects which set the
renderer uses:

```text
active_set 0:
  table 0  table 1
  table 2  table 3

active_set 1:
  table 4  table 5
  table 6  table 7
```

The simplest mode uses one active nametable. Pair modes use two nametables as a
larger scrolling surface. Mode `3` uses all four nametables in the active set,
giving an 80x50-cell surface, or 640x400 pixels, behind the 320x200 viewport:

| Mode | Meaning |
| - | - |
| `0` | Single 40x25 nametable using the active set's top-left table. |
| `1` | Horizontal 80x25 plane using the active set's top row. |
| `2` | Vertical 40x50 plane using the active set's left column. |
| `3` | Four-way 80x50 plane using the full 2x2 active set. |

For scrolling games, software normally updates offscreen rows or columns in the
active set while the viewport moves. The inactive set is optional staging space:
software can use it to prepare a new screen, menu, room, transition, or scratch
background and then switch `active_set` at the next frame boundary. Software
does not need to keep both sets synchronized during normal scrolling.

### Attribute Tables

Each background attribute table matches a background nametable cell-for-cell.
The overlay attribute table uses the same byte format and also matches its
overlay nametable cell-for-cell:

```text
1 attribute table   = 40 * 25 = 1000 bytes
4 background tables = 4000 bytes per scrolling set
8 background tables = 8000 bytes total
1 overlay table     = 1000 bytes
```

Recommended v1 attribute byte layout:

| Bits | Name | Description |
| - | - | - |
| `0-3` | `PAL` | Palette bank id, 0 to 15. |
| `4` | `FLIP_X` | Draw the character mirrored horizontally. |
| `5` | `FLIP_Y` | Draw the character mirrored vertically. |
| `6` | `PRIORITY` | Nonzero pixels of this cell are foreground pixels above sprites. |
| `7` | `CHR_ALT` | `0` = use the layer's primary character bank, `1` = use the layer's alternate character bank. |

`FLIP_X` and `FLIP_Y` reduce duplicate character art for mirrored slopes,
corners, borders, decorations, and UI pieces.

`PRIORITY` lets a background or overlay cell act like a foreground object. The
renderer still draws the cell as part of its tile layer, but its nonzero pixels
hide sprite pixels at the same screen positions. This is useful for scenery that
should appear in front of sprites, such as pillars, trees, door frames,
railings, UI frames, score bars, or dialog boxes. This is separate from sprite
priority: sprite priority moves a whole sprite behind all nonzero background
pixels, while cell priority marks specific cells as foreground.

`CHR_ALT` gives each background or overlay cell one extra character-bank
selection bit. The nametable byte still selects a character index from `0` to
`255`; `CHR_ALT` chooses whether that index is read from the layer's primary or
alternate character bank. For the scrolling background, those banks are
`bg_chr_bank` and `bg_chr_bank_alt`. For the fixed overlay, they are
`overlay_chr_bank` and `overlay_chr_bank_alt`. This allows a layer to mix two
256-character sets without widening the nametable.

### Fixed Overlay

The fixed overlay is a 40x25 screen-space tilemap intended for HUDs, scores,
menus, dialog boxes, and debug/status text. It has one nametable and one matching
attribute table:

```text
overlay nametable       = 40 * 25 = 1000 bytes
overlay attribute table = 40 * 25 = 1000 bytes
```

The overlay uses the same nametable and attribute byte formats as the scrolling
background, but it is not affected by `scroll_x` or `scroll_y`. Overlay cell
`(0, 0)` always maps to the top-left 8x8 pixels of the final 320x200 screen.

Overlay color index `0` is transparent, matching sprite transparency. Nonzero
overlay pixels are drawn with the selected palette. If an overlay cell's
`PRIORITY` bit is set, its nonzero pixels also hide sprite pixels at the same
screen positions. This gives software explicit control over whether sprites can
pass in front of or behind HUD elements.

### OAM

Object Attribute Memory stores 256 sprite records.

```text
1 sprite = 5 bytes
256 sprites = 1280 bytes
```

Recommended v1 sprite layout:

| Byte | Name | Description |
| - | - | - |
| `0` | `TILE` | Character index within the active sprite character bank. |
| `1` | `X` | Low 8 bits of the signed sprite screen X coordinate. |
| `2` | `Y` | Low 8 bits of the signed sprite screen Y coordinate. |
| `3` | `ATTR` | Palette and render attributes. |
| `4` | `EXT` | Extended coordinate bits and object flags. |

Recommended `ATTR` layout:

| Bits | Name | Description |
| - | - | - |
| `0-3` | `PAL` | Palette bank id, 0 to 15. |
| `4` | `PRIORITY` | `0` = in front of background, `1` = behind nonzero background pixels. |
| `5` | `FLIP_X` | Horizontal flip. |
| `6` | `FLIP_Y` | Vertical flip. |
| `7` | Reserved | Must be zero for v1. |

Recommended `EXT` layout:

| Bits | Name | Description |
| - | - | - |
| `0-1` | `X_HI` | Bits 8-9 of a signed 10-bit sprite screen X coordinate. |
| `2` | `Y_HI` | Bit 8 of a signed 9-bit sprite screen Y coordinate. |
| `3` | `DISABLE` | `1` hides the sprite. |
| `4-7` | Reserved | Must be zero for v1. |

The renderer sign-extends X from 10 bits and Y from 9 bits. This gives sprites
enough range to enter and leave the 320x200 viewport smoothly:

```text
X range: -512 to 511
Y range: -256 to 255
```

Sprite coordinates are screen-space coordinates, relative to the top-left corner
of the final 320x200 viewport. They are signed so sprites can be partially
offscreen to the left or top without special cases. If a game tracks objects in
world coordinates, the 6502 computes screen-space OAM coordinates before writing
the sprite record:

```text
sprite_screen_x = object_world_x - scroll_x
sprite_screen_y = object_world_y - scroll_y
```

## Proposed MIA RAM Layout

This layout fits inside the current 128 KiB MIA RAM region.

| Start | Size | End | Description |
| - | - | - | - |
| `0x00000` | `0x0100` | `0x000FF` | Video control block |
| `0x00100` | `0x0100` | `0x001FF` | 16 palette banks |
| `0x00200` | `0xC000` | `0x0C1FF` | 8 character banks |
| `0x0C200` | `0x1F40` | `0x0E13F` | 8 background nametables |
| `0x0E140` | `0x1F40` | `0x1007F` | 8 background attribute tables |
| `0x10080` | `0x0500` | `0x1057F` | Sprite OAM |
| `0x10580` | `0x0100` | `0x1067F` | Dirty flags and generation counters |
| `0x10680` | `0x03E8` | `0x10A67` | Overlay nametable |
| `0x10A68` | `0x03E8` | `0x10E4F` | Overlay attribute table |
| `0x10E50` | `0x01B0` | `0x10FFF` | Reserved video expansion |
| `0x11000` | `0x0F000` | `0x1FFFF` | Free MIA RAM for other uses |

The video region consumes about 68 KiB, leaving about 60 KiB for non-video MIA
features and user data.

## Preconfigured Indexes

Video reserves a stable range of indexes from the 256-index descriptor table.

| Index | Purpose | Default address | Length |
| - | - | - | - |
| `16-23` | Character banks 0-7 | `0x00200 + bank * 0x1800` | `0x1800` |
| `32-47` | Palette banks 0-15 | `0x00100 + bank * 0x10` | `0x10` |
| `48-55` | Background nametables 0-7 | `0x0C200 + table * 1000` | `1000` |
| `56-63` | Background attribute tables 0-7 | `0x0E140 + table * 1000` | `1000` |
| `64` | Sprite OAM | `0x10080` | `1280` |
| `65` | Video control block | `0x00000` | `256` |
| `66` | Dirty flags | `0x10580` | `256` |
| `67` | Overlay nametable | `0x10680` | `1000` |
| `68` | Overlay attribute table | `0x10A68` | `1000` |
| `69-79` | Reserved video indexes | varies | varies |

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
client. Multi-byte fields are little-endian. The network packet header carries
the `MIAV` signature and protocol version, so the RAM control block starts
directly with drawing state. Frame presentation and publisher bookkeeping come
after the render-facing fields.

Drawing state:

| Offset | Size | Name | Description |
| - | - | - | - |
| `0x00` | 1 | `mode` | Selects the overall video engine. `0` = disabled, `1` = tile/sprite mode at 320x200. |
| `0x01` | 1 | `active_set` | `0` = tables 0-3, `1` = tables 4-7. |
| `0x02` | 1 | `viewport_mode` | `0` single table, `1` horizontal pair, `2` vertical pair, `3` 2x2 four-way plane. |
| `0x03` | 1 | `render_flags` | Render enable bits. |
| `0x04` | 2 | `scroll_x` | Unsigned top-left viewport X position in pixels within the active background plane. |
| `0x06` | 2 | `scroll_y` | Unsigned top-left viewport Y position in pixels within the active background plane. |
| `0x08` | 1 | `bg_chr_bank` | Primary character bank used for background tiles. |
| `0x09` | 1 | `bg_chr_bank_alt` | Alternate background character bank used when cell attribute `CHR_ALT` is set. |
| `0x0A` | 1 | `sprite_chr_bank` | Character bank used for sprites. |
| `0x0B` | 1 | `bg_color` | Optional backdrop palette index. |
| `0x0C` | 1 | `overlay_chr_bank` | Primary character bank used for overlay tiles. |
| `0x0D` | 1 | `overlay_chr_bank_alt` | Alternate overlay character bank used when overlay cell attribute `CHR_ALT` is set. |
| `0x0E` | 1 | `chr_1bpp_mask` | Bit mask selecting which character banks decode as 1bpp. |
| `0x0F` | 1 | `chr_1bpp_planes` | Plane selectors for 1bpp background, sprite, and overlay rendering. |

`mode` selects how the whole video memory region should be interpreted. In v1,
only two values are defined:

| Value | Meaning |
| - | - |
| `0` | Video disabled. The client should not render a screen from this state. |
| `1` | Tile/sprite mode. The client renders a 320x200 viewport from character banks, palettes, background tables, overlay tables, and OAM. |

`viewport_mode` is separate. It only controls how many nametables make up the
background plane inside tile/sprite mode. For example, `mode = 1` and
`viewport_mode = 3` means "use the tile/sprite renderer, with four nametables
arranged as one 80x50-cell scrolling plane."

`scroll_x` and `scroll_y` are unsigned 16-bit background-space coordinates. They
are the complete smooth-scroll position. MIA does not need separate coarse and
fine scroll fields because the renderer can derive them directly:

```text
coarse_col = scroll_x >> 3
fine_x     = scroll_x & 7

coarse_row = scroll_y >> 3
fine_y     = scroll_y & 7
```

In four-way mode, the active background plane is 80x50 cells, or 640x400 pixels.
The renderer uses the coarse tile position to choose the nametable cell and the
fine pixel position to draw partially visible edge tiles. For example,
`scroll_x = 20` and `scroll_y = 12` starts the viewport two full tiles plus four
pixels from the left, and one full tile plus four pixels from the top.

This gives the video state two explicit coordinate spaces:

| Field | Coordinate space | Signed? | Meaning |
| - | - | - | - |
| `scroll_x`, `scroll_y` | Background plane | No | Where the viewport starts inside the active nametable plane. |
| OAM `X`, `Y` | Screen/viewport | Yes | Where a sprite is drawn on the final 320x200 screen. |

Recommended `render_flags` bits:

| Bit | Name | Description |
| - | - | - |
| `0` | `ENABLE_BG` | Render background. |
| `1` | `ENABLE_SPRITES` | Render sprites. |
| `2` | `ENABLE_OVERLAY` | Render the fixed overlay tilemap. |
| `3-7` | Reserved | Must be zero for v1. |

Recommended `chr_1bpp_mask` bits:

| Bit | Name | Description |
| - | - | - |
| `0-7` | `BANK_N_1BPP` | Bit `N` selects 1bpp decoding for character bank `N`; clear means normal 3bpp decoding. |

Recommended `chr_1bpp_planes` layout:

| Bits | Name | Description |
| - | - | - |
| `0-1` | `BG_PLANE` | Character plane used when the selected background bank is 1bpp. |
| `2-3` | `SPRITE_PLANE` | Character plane used when the selected sprite bank is 1bpp. |
| `4-5` | `OVERLAY_PLANE` | Character plane used when the selected overlay bank is 1bpp. |
| `6-7` | Reserved | Must be zero for v1. |

For each plane selector, values `0`, `1`, and `2` select character planes 0, 1,
and 2. Value `3` is reserved. A layer uses its plane selector only when the
selected character bank's bit is set in `chr_1bpp_mask`; otherwise the bank is
decoded as normal 3bpp character data.

Frame and publisher state:

| Offset | Size | Name | Description |
| - | - | - | - |
| `0x10` | 1 | `target_fps` | Suggested publish rate, usually 25 or 30. |
| `0x11` | 1 | `present_flags` | Frame present hints. |
| `0x12` | 2 | `frame_id` | Incremented by the 6502 when a frame is ready. |
| `0x14` | 1 | `sync_flags` | Publisher and synchronization hints. |
| `0x15` | 1 | `reserved_sync` | Reserved for future sync state. |
| `0x16` | 2 | `dirty_mask_low` | Optional dirty region bits, low word. |
| `0x18` | 2 | `dirty_mask_high` | Optional dirty region bits, high word. |
| `0x1A` | 6 | `reserved` | Reserved for timing and transport metadata. |
| `0x20` | 224 | `future` | Reserved. |

Recommended `present_flags` bits:

| Bit | Name | Description |
| - | - | - |
| `0` | `PRESENT` | 6502 toggles or sets when a coherent frame is ready. |
| `1` | `FORCE_FULL_FRAME` | Publisher should send full active background tables, overlay tables, and OAM. |
| `2` | `FORCE_RESOURCE_SYNC` | Publisher should send all palettes and character banks. |
| `3-7` | Reserved | Must be zero for v1. |

Recommended `sync_flags` bits:

| Bit | Name | Description |
| - | - | - |
| `0` | `REQUEST_KEYFRAME` | Client or 6502 may request a full sync. |
| `1` | `FRAME_LOCK` | 6502 is updating active state; publisher should wait. |
| `2-7` | Reserved | Must be zero for v1. |

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

Character bank 0 uses index `16`. Because character data is planar, character
`N` has one 8-byte row block in each plane:

```text
plane 0 rows: 0x00200 + 0x0000 + N * 8
plane 1 rows: 0x00200 + 0x0800 + N * 8
plane 2 rows: 0x00200 + 0x1000 + N * 8
```

Each block stores rows `0` through `7`. A full-bank stream writes all of plane
`0`, then all of plane `1`, then all of plane `2`.

When a bank is flagged as 1bpp, those same three planes become three independent
monochrome character tables. Software can update only the plane it is using for
a font, HUD icon set, sprite mask, or other 1bpp art.

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

For table `T`, use:

```text
0x0C200 + T * 1000 + y * 40 + x
```

The matching attribute table uses index `56 + T` and starts at:

```text
0x0E140 + T * 1000 + y * 40 + x
```

### Updating an Overlay Cell

The overlay nametable uses index `67`. Cell `(x, y)` starts at:

```text
0x10680 + y * 40 + x
```

The matching overlay attribute table uses index `68` and starts at:

```text
0x10A68 + y * 40 + x
```

Overlay cell coordinates are screen-space tile coordinates. Cell `(0, 0)` is
always the top-left tile of the final 320x200 screen.

For efficient random cell updates, the firmware should eventually expose one of
these mechanisms:

- A command to set the current address of any index.
- A command to seek a preconfigured video index by offset.
- A small video command queue in MIA RAM.

Until that exists, sequential writes are easy and random writes are awkward.

### Presenting a Frame

When using the inactive 2x2 set for a page flip, the 6502 should finish writing
that set, then write the control block fields:

```text
active_set = next set
frame_id   = frame_id + 1
present_flags.PRESENT toggled or set
```

For scrolling within the active set, software can instead update offscreen rows
or columns, change `scroll_x` and `scroll_y`, and present a new `frame_id`.

The network publisher treats `frame_id` changes as frame boundaries.

## Dirty Tracking

A simple publisher can send a complete active mode-3 frame package at every
publish interval:

```text
4 background nametables = 4000 bytes
4 background attributes = 4000 bytes
overlay nametable       = 1000 bytes
overlay attribute table = 1000 bytes
OAM                     = 1280 bytes
control block           = 256 bytes
total                   = 11536 bytes, about 11.3 KiB per frame
```

At 25 FPS this is about 282 KiB/s. At 30 FPS this is about 338 KiB/s. That is
still well within a realistic Wi-Fi budget for Pico W-class hardware.

After the full-frame path works, add dirty tracking:

| Region | Recommended dirty granularity |
| - | - |
| Palette banks | 1 palette bank, 16 bytes |
| Character banks | 1 3bpp character across 3 planes, 24 bytes total; or 1 1bpp character in one plane, 8 bytes |
| Background nametables | range of cells or 40-byte row |
| Background attribute tables | range of cells or 40-byte row |
| Overlay nametable | range of cells or 40-byte row |
| Overlay attribute table | range of cells or 40-byte row |
| OAM | 1 sprite record, 5 bytes |
| Control block | whole block or changed range |

Dirty marking must stay cheap. If marking happens in the index write path, it
should only set a byte, bit, or generation counter. The main loop can later
expand those flags into packets.

An alternative is shadow comparison: the publisher keeps a shadow copy of video
regions and diffs them every frame. That keeps the bus path clean but costs more
background CPU time. For v1, a hybrid approach is reasonable:

- Send full active frame packages first.
- Add explicit dirty bits for palettes, OAM, and control.
- Add row-level or tile-level dirty tracking for background and overlay tables.
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
| `0x04` | Background nametable |
| `0x05` | Background attribute table |
| `0x06` | OAM |
| `0x07` | Overlay nametable |
| `0x08` | Overlay attribute table |
| `0x09` | Dirty metadata |

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
- Per-frame overlay table updates
- Frame boundary packets

Recommended v1 behavior:

- Client requests a full snapshot when it detects missing snapshot chunks.
- Client requests individual resources when critical chunks are missing.
- MIA sends periodic keyframes, such as every 1 to 5 seconds.
- The client and publisher should never queue stale frame updates to preserve
  playback order. The newest coherent `frame_id` wins.
- MIA may ignore stale frame updates if a newer `frame_id` has arrived.
- Client renders the newest coherent state it has.

This avoids TCP head-of-line blocking while still allowing the client to recover
from packet loss. It also bounds display latency: a congested link may drop
intermediate frames, but it should not become a delayed video feed.

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
8 background nametables
8 background attribute tables
overlay nametable
overlay attribute table
OAM
```

Rendering steps:

1. Clear the output to the backdrop color.
2. Render background tiles if `render_flags.ENABLE_BG` is set, applying each
   cell's palette, flip, character-bank, and priority attributes. Track all
   nonzero background pixels for sprite priority, and track nonzero pixels from
   `PRIORITY` cells in a foreground mask.
3. Render overlay tiles if `render_flags.ENABLE_OVERLAY` is set. Overlay color
   index `0` is transparent. Track nonzero pixels from overlay `PRIORITY` cells
   in the same foreground mask.
4. Render sprites with transparency if `render_flags.ENABLE_SPRITES` is set.
   Sprite pixels should be hidden behind nonzero background pixels when the
   sprite's `PRIORITY` bit requests it, and behind nonzero background or
   overlay pixels already marked in the foreground mask.
5. Present the final 320x200 image, usually scaled to a larger window.

The client can be written in Go, TypeScript, Python, C, or any language with UDP
and a simple pixel surface. The Go emulator repo is a natural place for the
first client because it already models Clementina and can share data structures.

## Bandwidth Estimates

Full active frame package:

| Data | Bytes |
| - | - |
| Four background nametables | 4000 |
| Four background attribute tables | 4000 |
| Overlay nametable | 1000 |
| Overlay attribute table | 1000 |
| OAM | 1280 |
| Control block | 256 |
| Total payload | 11536 |

Approximate sustained payload:

| FPS | Payload/sec |
| - | - |
| 25 | 288400 bytes/sec |
| 30 | 346080 bytes/sec |

Full resource snapshot:

| Data | Bytes |
| - | - |
| Character banks | 49152 |
| Palette banks | 256 |
| Background nametables | 8000 |
| Background attribute tables | 8000 |
| Overlay nametable | 1000 |
| Overlay attribute table | 1000 |
| OAM | 1280 |
| Control block | 256 |
| Total payload | 68944 |

Even a full snapshot is only about 67 KiB. Sending that on connect or as an
occasional keyframe is reasonable. Sending all character data every frame is not
necessary and should be avoided.

## Firmware Work Plan

### Phase 1: Static Video Memory Layout

- Add `video` constants for the memory map and index ids.
- Preconfigure indexes 16-23, 32-47, 48-55, 56-63, 64, 65, 66, 67, and 68.
- Initialize the video control block with mode, drawing defaults, and default FPS.
- Add tests or debug routines that prove the regions fit in 128 KiB.

### Phase 2: Local Client and Snapshot Format

- Implement the packet structs in shared documentation and client code.
- Build a desktop client that can render backgrounds, overlay, sprites, and
  1bpp/3bpp character banks from a captured snapshot.
- Add a firmware debug path to send a full snapshot over UDP.
- Keep packet payloads <= 512 bytes.

### Phase 3: Live Wi-Fi Stream

- Initialize Wi-Fi in AP or STA mode.
- Add `cyw43_arch_poll()` to the main loop.
- Add UDP `HELLO`/`WELCOME`.
- Send a full snapshot on connection.
- Send full active frame packages at 25 FPS.

### Phase 4: Dirty Updates

- Add dirty flags/generation counters for palettes, control, OAM, background
  tables, and overlay tables.
- Add resource update packets.
- Add periodic keyframes.
- Add client-side resource requests for missed critical chunks.

### Phase 5: Richer PPU Behavior

- Add horizontal, vertical, and four-way scrolling modes.
- Add sprite priority rules and optional collision status.
- Add 8x16 sprite mode if needed.
- Add optional compression for long repeated table regions.

## Open Questions

- Should v1 target 25 FPS, 30 FPS, or allow both through the control block?
- Should MIA default to AP mode for easy client connection, or STA mode for home
  network development?
- Should the 6502 signal frame readiness by changing `frame_id`, toggling a
  `PRESENT` bit, or issuing a future command?
- Should arbitrary video index seeking be added as commands, config fields, or a
  small video command queue?
- Should the first client live in the Go emulator repo or as a small standalone
  viewer in this firmware repo?

## Summary

The feasible path is a remote PPU, not a remote framebuffer. MIA stores compact
tile, palette, background, overlay, and sprite state in its 128 KiB RAM.
Clementina writes that state through the existing indexed memory interface. MIA
publishes state snapshots and updates over UDP. The client renders locally.

This keeps bandwidth low, fits the current memory budget, respects the
time-critical bus architecture, and gives Clementina a video model that feels
like a period-appropriate graphics chip rather than a pixel streaming device.
