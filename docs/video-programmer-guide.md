# MIA Video Programmer Guide

This guide describes how a Clementina 6502 program uses MIA Wi-Fi video output.

The short version:

- Write video state through MIA's indexed RAM windows.
- Use fast video indexes for hot registers such as scroll, banks, OAM, palettes,
  and nametables.
- Call `VIDEO_COMMIT_FRAME` when the current frame is ready.
- Ignore `VIDEO_CAN_COMMIT` for a local/free-running game.
- Wait for `VIDEO_CAN_COMMIT` for a video-paced game.

## Mental Model

MIA behaves like a remote PPU. Your program writes tile maps, character
graphics, palettes, sprites, scroll values, and overlay text into MIA RAM. The
client mirrors that state and renders the pixels on a host computer.

You are not drawing pixels into a framebuffer. You are updating graphics state.

## MIA Registers

MIA occupies `$FFE0-$FFFF`.

| Register | Address | Use |
| --- | ---: | --- |
| `IDXA_PORT` | `$FFE0` | read/write through selected index A |
| `IDXA_SELECTOR` | `$FFE1` | select index descriptor for window A |
| `CFG_SELECTOR` | `$FFE2` | select configuration field |
| `CFG_PORT` | `$FFE3` | read/write selected configuration field |
| `IDXB_PORT` | `$FFE4` | read/write through selected index B |
| `IDXB_SELECTOR` | `$FFE5` | select index descriptor for window B |
| `CMD_PARAM1` | `$FFE6` | command parameter 1 |
| `CMD_PARAM2` | `$FFE7` | command parameter 2 |
| `CMD_PARAM3` | `$FFE8` | command parameter 3 |
| `CMD_TRIGGER` | `$FFE9` | command id; writing triggers the command |
| `MIA_STATUS` | `$FFEA-$FFEB` | general MIA status |
| `MIA_ERROR` | `$FFEC-$FFED` | error queue |
| `IRQ_MASK` | `$FFEE-$FFEF` | interrupt mask |
| `IRQ_STATUS` | `$FFF0-$FFF1` | interrupt status |

Video status lives in the video control block, not in `MIA_STATUS`. Use
`VIDX_VIDEO_STATUS` to read it quickly.

The video event IRQ source is `IRQ_VIDEO_EVENT = $0020`. Enable that bit in
`IRQ_MASK` to let video events drive the 6502 IRQ line.

## Video Commands

Video commands use the normal command registers.

| Command | Id | Purpose |
| --- | ---: | --- |
| `VIDEO_ENABLE` | `$40` | initialize video state and fast video indexes |
| `VIDEO_COMMIT_FRAME` | `$41` | commit the current dirty video state as one frame |
| `VIDEO_REQUEST_SNAPSHOT` | `$42` | force the active client to resync from a snapshot |
| `VIDEO_SET_MODE` | `$43` | update `VIDEO_MODE` bits |

Committing a frame:

```asm
VIDEO_COMMIT_FRAME = $41

video_commit_frame:
    lda #$00
    sta $FFE6       ; CMD_PARAM1: flags
    stz $FFE7       ; CMD_PARAM2
    stz $FFE8       ; CMD_PARAM3
    lda #VIDEO_COMMIT_FRAME
    sta $FFE9       ; CMD_TRIGGER
    rts
```

`VIDEO_COMMIT_FRAME` is the frame boundary. Writes made before the command
belong to that frame. Writes made after the command belong to later frames.

## Video Control Block

The video control block starts at MIA RAM offset `$00000`. The full layout is in
[video-output.md](video-output.md).

Important fields:

| Offset | Field | Meaning |
| ---: | --- | --- |
| `$01` | `VIDEO_MODE` | video enable and renderer mode bits |
| `$02` | `VIDEO_STATUS` | connection and backpressure bits |
| `$03` | `LAYER_ENABLE` | background, overlay, sprite enables |
| `$04-$07` | `FRAME_ID` | accepted frame counter |
| `$08-$09` | `SCROLL_X` | background scroll X |
| `$0A-$0B` | `SCROLL_Y` | background scroll Y |
| `$0C` | `BG_ACTIVE_SET` | active 2x2 background set |
| `$0D` | `BG_SCROLL_MODE` | background plane mode |
| `$0E-$12` | bank selectors | bg, bg alt, overlay, overlay alt, sprite |
| `$14-$15` | `OAM_ACTIVE_COUNT` | active sprite record count |
| `$18` | `VIDEO_IRQ_ENABLE` | enabled video event IRQ sources |
| `$19` | `VIDEO_EVENT_STATUS` | pending video events; write `1` bits to clear |

`VIDEO_STATUS` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_CLIENT_CONNECTED` | a client owns the video session |
| 1 | `VIDEO_CAN_COMMIT` | MIA can accept another non-coalesced frame |
| 2 | `VIDEO_SNAPSHOT_ACTIVE` | snapshot response in progress |
| 3 | `VIDEO_REPAIR_ACTIVE` | repair response in progress |
| 4 | `VIDEO_FRAME_QUEUE_FULL` | pending frame storage is full |

## Fast Video Indexes

Fast video indexes are preconfigured by `VIDEO_ENABLE`. Select one index, then
stream bytes through `IDXA_PORT` or `IDXB_PORT`. Each index steps after a write
and wraps at its limit.

| Index | Name | Length | Use |
| ---: | --- | ---: | --- |
| `$80` | `VIDX_SCROLL_X` | 2 | write `SCROLL_X` low, high, repeat |
| `$81` | `VIDX_SCROLL_Y` | 2 | write `SCROLL_Y` low, high, repeat |
| `$82` | `VIDX_BG_PLANE` | 2 | write active background set and scroll mode |
| `$83` | `VIDX_BANK_SELECT` | 5 | write bg, bg alt, overlay, overlay alt, sprite banks |
| `$84` | `VIDX_LAYER_ENABLE` | 1 | write layer enable flags |
| `$85` | `VIDX_OAM_COUNT` | 2 | write active sprite count low, high, repeat |
| `$86` | `VIDX_FRAME_FLAGS` | 2 | write frame flags and commit flags |
| `$87` | `VIDX_VIDEO_STATUS` | 1 | read video status |
| `$88` | `VIDX_PALETTE` | 256 | stream palette bytes |
| `$89` | `VIDX_OAM` | 1,280 | stream sprite records |
| `$8A` | `VIDX_OVERLAY_NT` | 1,000 | stream overlay nametable |
| `$8B` | `VIDX_OVERLAY_ATTR` | 1,000 | stream overlay attributes |
| `$90-$97` | `VIDX_CHR_BANK_0-7` | 6,144 | stream one CHR bank |
| `$A0-$A7` | `VIDX_BG_NT_0-7` | 1,000 | stream one background nametable |
| `$A8-$AF` | `VIDX_BG_ATTR_0-7` | 1,000 | stream one background attribute table |

This is the fast path for frequently changed fields. You do not configure an
address, limit, step, or wrap mode for these indexes; MIA has already done it.

## Two-Byte Register Writes

The wrapped indexes make 16-bit video registers cheap to update.

```asm
VIDX_SCROLL_X = $80
IDXA_PORT     = $FFE0
IDXA_SELECTOR = $FFE1

select_scroll_x:
    lda #VIDX_SCROLL_X
    sta IDXA_SELECTOR
    rts

write_scroll_x:
    lda scroll_x
    sta IDXA_PORT        ; low byte
    lda scroll_x+1
    sta IDXA_PORT        ; high byte, index wraps to low byte
    rts
```

After the second write, the index points back at the low byte. The next call can
write the same two bytes again without reselecting or reconfiguring the index.

Use both windows for common paired updates:

```asm
VIDX_SCROLL_X = $80
VIDX_SCROLL_Y = $81

    lda #VIDX_SCROLL_X
    sta $FFE1           ; index A = scroll X
    lda #VIDX_SCROLL_Y
    sta $FFE5           ; index B = scroll Y

update_scroll:
    lda scroll_x
    sta $FFE0
    lda scroll_x+1
    sta $FFE0

    lda scroll_y
    sta $FFE4
    lda scroll_y+1
    sta $FFE4
    rts
```

## Loading Initial Video State

A program normally initializes video in this order:

1. issue `VIDEO_ENABLE`,
2. load palettes through `VIDX_PALETTE`,
3. load CHR banks through `VIDX_CHR_BANK_0-7`,
4. fill background nametables and attributes,
5. fill overlay nametable and attributes,
6. initialize OAM,
7. set scroll, layer flags, and active banks,
8. call `VIDEO_COMMIT_FRAME`,
9. enter the main loop.

The first client receives a full snapshot. Large startup writes affect startup
time, not steady-state bandwidth.

## Local Mode

In local/free-running mode, the game loop does not wait for video.

```asm
main_loop:
    jsr read_input
    jsr update_game
    jsr draw_video_state
    jsr video_commit_frame
    jmp main_loop
```

The simulation keeps its own pace. If the network falls behind, MIA coalesces
old unsent visual state and the client jumps to a newer frame later.

Use this mode when local simulation timing matters more than remote display
smoothness.

## Video-Paced Mode

In video-paced mode, the game waits until MIA can accept a frame without
coalescing.

```asm
VIDX_VIDEO_STATUS = $87
VIDEO_CAN_COMMIT = %00000010

wait_video_can_commit:
    lda #VIDX_VIDEO_STATUS
    sta $FFE1

wait_loop:
    lda $FFE0
    and #VIDEO_CAN_COMMIT
    beq wait_loop
    rts

main_loop:
    jsr wait_video_can_commit
    jsr read_input
    jsr update_game
    jsr draw_video_state
    jsr video_commit_frame
    jmp main_loop
```

This makes the client/network pace the game. If Wi-Fi is slow, the game runs
slower, but accepted frame boundaries are retained. This is similar in spirit
to old machines where game logic synchronizes to raster or vblank timing.

## What VIDEO_CAN_COMMIT Means

`VIDEO_CAN_COMMIT` means MIA has room to accept another frame boundary without
intentional coalescing.

MIA calculates it from:

- video enable state,
- active client state,
- accepted `max_in_flight`,
- pending frame queue space,
- snapshot and repair pressure,
- packet staging memory.

It does not mean the client has already displayed the previous frame. It means
MIA can safely accept the next `VIDEO_COMMIT_FRAME`.

When no client is connected, MIA keeps `VIDEO_CAN_COMMIT` set so a
video-paced program continues to run while disconnected. The separate
`VIDEO_CLIENT_CONNECTED` bit tells a monitor or demo if a client is attached.

`VIDEO_CAN_COMMIT` changes from set to clear when MIA runs out of room for
another distinct frame commit. With a one-slot queue, this normally happens
immediately after `VIDEO_COMMIT_FRAME`. With a deeper queue, it happens only
after the last retained-frame slot is filled.

`VIDEO_CAN_COMMIT` changes from clear to set when MIA has room again. This
usually happens after the client acknowledges a frame response, a repair
finishes, a snapshot finishes, or packet staging memory becomes available.

Calling `VIDEO_COMMIT_FRAME` while `VIDEO_CAN_COMMIT` is clear is valid for a
local/free-running game. MIA coalesces the newest dirty state into the latest
pending frame instead of queueing a distinct frame. The client can skip visual
frames, but it stays synchronized.

## Video IRQ

MIA can raise an IRQ when selected video events occur. This lets a program do
other work while waiting for video, or use the IRQ as the cadence source for a
video-paced loop.

Video event bits live in the video control block:

| Bit | Event | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_EVENT_CAN_COMMIT_RISE` | `VIDEO_CAN_COMMIT` changed from clear to set |
| 1 | `VIDEO_EVENT_CAN_COMMIT_FALL` | `VIDEO_CAN_COMMIT` changed from set to clear |
| 2 | `VIDEO_EVENT_CLIENT_CHANGE` | client connection state changed |
| 3 | `VIDEO_EVENT_RESYNC` | client entered snapshot/resync |

`VIDEO_IRQ_ENABLE` selects which event bits raise the video IRQ. Latched events
appear in `VIDEO_EVENT_STATUS`; write `1` bits to clear them. When any enabled
event is pending, MIA sets `IRQ_VIDEO_EVENT` (`$0020`) in `IRQ_STATUS`. The
normal `IRQ_MASK` register controls delivery to the 6502 IRQ line.

To wake when a frame commit becomes available, enable
`VIDEO_EVENT_CAN_COMMIT_RISE` in `VIDEO_IRQ_ENABLE` and enable
`IRQ_VIDEO_EVENT` in `IRQ_MASK`.

## Cheap and Expensive Updates

Cheap per-frame updates:

- scroll registers,
- sprite positions,
- OAM records,
- a few nametable cells,
- palette cycling,
- small overlay text.

Expensive updates:

- many nametable rows,
- many attribute rows,
- large CHR edits,
- full-screen tile animation.

CHR edits are legal and bandwidth-heavy. One 8x8 3bpp character is 24 data
bytes before protocol overhead. A whole 256-character bank is 6 KiB.

## Avoiding Artifacts

Build a complete frame in MIA video memory, then call `VIDEO_COMMIT_FRAME`.

For video-paced loops, wait for `VIDEO_CAN_COMMIT` before starting the next
frame's writes. This gives MIA a clean frame boundary and enough queue space to
retain the committed state.

For local loops, continuous writes are valid. The client can skip visual frames
when the network falls behind, but absolute updates and snapshot repair prevent
permanent desync.

## Debugging

- Old graphics on the client: request a snapshot from the client or issue
  `VIDEO_REQUEST_SNAPSHOT`.
- Slow game only in video-paced mode: the client/network is the limiter.
- Large payload spikes: reduce per-frame CHR edits or preload more CHR banks.
- Rising frame lag: lower client FPS or increase `max_in_flight` up to 4.
- Frequent repairs: lower FPS, lower payload size, or improve the Wi-Fi link.
