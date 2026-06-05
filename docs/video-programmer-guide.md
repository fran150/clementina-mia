# MIA Video Programmer Guide

This guide describes how a Clementina 6502 program uses MIA Wi-Fi video output.

The short version:

- Write video state through MIA's indexed RAM windows.
- Use fast video indexes for hot registers such as scroll, banks, OAM, palettes,
  and nametables.
- The client requests video updates; the 6502 does not explicitly commit frames.
- MIA marks 32-byte video pages dirty as you write them.
- Avoid changing visible memory during MIA readout or before client
  acknowledgement if you want artifact-free output.
- Use video status bits or IRQ events to pace strict update loops.

## Mental Model

MIA behaves like a remote video processor. Your program writes tile maps,
character graphics, palettes, sprites, scroll values, and overlay text into MIA
RAM. The client mirrors that state and renders the pixels on a host computer.

You are not drawing pixels into a framebuffer. You are updating graphics state.
MIA tracks which 32-byte pages of that graphics state changed. When the client
requests an update, MIA sends the dirty pages to the client and starts tracking
new writes for the following update.

The mirrored video region is 68,944 bytes. MIA divides it into 2,155 pages of
32 bytes each and tracks those pages with a 270-byte dirty map. Each bit in that
map means "this page changed." Two maps are used: one collects your current
writes, while the other names the pages being sent or repaired for the active
client update.

This is similar to classic video hardware in one important way: if you change
visible video memory while the display system is reading it, you can get a
temporary artifact. MIA exposes status bits and IRQ events so your program can
choose how strict it wants to be.

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
| `VIDEO_FORCE_FULL_REFRESH` | `$42` | mark all video pages dirty for the next client update |
| `VIDEO_SET_MODE` | `$43` | update `VIDEO_MODE` bits |

There is no frame commit command in the current video model. The client
requests updates, and MIA publishes the pages that were dirty at the moment the
request was accepted.

Forcing a full refresh:

```asm
VIDEO_FORCE_FULL_REFRESH = $42

video_force_full_refresh:
    stz $FFE6       ; CMD_PARAM1
    stz $FFE7       ; CMD_PARAM2
    stz $FFE8       ; CMD_PARAM3
    lda #VIDEO_FORCE_FULL_REFRESH
    sta $FFE9       ; CMD_TRIGGER
    rts
```

A full refresh is not a frozen snapshot. It schedules the next accepted update
to include every video page. MIA sends the current contents of those pages when
the client requests that update.

## Video Control Block

The video control block starts at MIA RAM offset `$00000`. The full layout is in
[video-output.md](video-output.md).

Important fields:

| Offset | Field | Meaning |
| ---: | --- | --- |
| `$01` | `VIDEO_MODE` | video enable and renderer mode bits |
| `$02` | `VIDEO_STATUS` | connection and update lifecycle bits |
| `$03` | `LAYER_ENABLE` | background, overlay, sprite enables |
| `$04-$07` | `FRAME_ID` | latest assigned client update id |
| `$08-$09` | `SCROLL_X` | background scroll X |
| `$0A-$0B` | `SCROLL_Y` | background scroll Y |
| `$0C` | `BG_ACTIVE_SET` | active 2x2 background set |
| `$0D` | `BG_SCROLL_MODE` | background plane mode |
| `$0E-$12` | bank selectors | bg, bg alt, overlay, overlay alt, sprite |
| `$14-$15` | `OAM_ACTIVE_COUNT` | active sprite record count |
| `$18` | `VIDEO_IRQ_ENABLE` | enabled video event IRQ sources |
| `$19` | `VIDEO_EVENT_STATUS` | pending video events; write `1` bits to clear |

`FRAME_ID` starts at `0` for a session, increments when MIA accepts a client
request that produces an update, and wraps from `0xFFFFFFFF` to `1`.

`VIDEO_STATUS` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_CLIENT_CONNECTED` | a client owns the video session |
| 1 | `VIDEO_UPDATE_ACTIVE` | an accepted update is not yet acknowledged |
| 2 | `VIDEO_READOUT_ACTIVE` | MIA is reading live video RAM for send or repair |
| 3 | `VIDEO_RESPONSE_SENT` | initial response send finished; ACK may still be pending |
| 4 | `VIDEO_FULL_REFRESH_PENDING` | the next update will include all video pages |
| 5 | `VIDEO_REPAIR_ACTIVE` | repair chunks are being regenerated/sent |

The most conservative clean-output rule is:

```text
do not change visible memory while VIDEO_UPDATE_ACTIVE is set
```

That waits until the client has acknowledged the update, so later repairs cannot
pick up newer visible values. A lower-latency rule is:

```text
do not change visible memory while VIDEO_READOUT_ACTIVE is set
```

That avoids artifacts during the first send but allows repair-time artifacts if
the network loses chunks.

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
| `$86` | `VIDX_FRAME_FLAGS` | 1 | write render frame flags |
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
8. enter the main loop.

The first client update after connection is a full refresh because MIA marks
every video page dirty. Large startup writes affect startup transfer time, not
steady-state bandwidth.

## Update Lifecycle

MIA and the client loop through this lifecycle:

```text
nothing pending
client requests update
MIA rotates active dirty set to pending and starts readout
MIA sends dirty page records
MIA finishes first send
client repairs missing chunks if needed
client applies the complete update
client ACKs the frame
MIA clears the retained pending dirty set
nothing pending
```

During the lifecycle:

- `VIDEO_EVENT_FRAME_REQUEST` fires when MIA accepts the request.
- `VIDEO_UPDATE_ACTIVE` is set from accepted request until ACK.
- `VIDEO_READOUT_ACTIVE` is set while MIA is reading video RAM for send/repair.
- `VIDEO_EVENT_FRAME_SENT` fires when the first complete send has finished.
- `VIDEO_EVENT_FRAME_ACKED` fires after the client acknowledges the update and
  MIA has already cleared the retained dirty set used for repair.

Changing visible memory at different points has different tradeoffs:

| When you write visible memory | Result |
| --- | --- |
| before request | included in the next update |
| during `VIDEO_READOUT_ACTIVE` | may appear partially in the current update |
| after first send but before ACK | may appear in repair chunks for that update |
| after ACK | belongs cleanly to a later update |

Writes to inactive or non-visible resources are safe as long as they cannot
affect the update currently being read. For example, loading an unused CHR bank
is safe until the same visible update selects that bank.

## Free-Running Mode

In free-running mode, the game loop ignores video timing.

```asm
main_loop:
    jsr read_input
    jsr update_game
    jsr draw_video_state
    jmp main_loop
```

The simulation keeps its own pace. MIA keeps marking dirty pages. If the client
or network falls behind, the next response includes the accumulated dirty pages.
The client may see transient artifacts if visible memory changes while MIA is
reading it, but the mirror converges on later updates.

Use this mode when local responsiveness matters more than remote display
cleanliness.

## Readout-Paced Mode

In readout-paced mode, the game avoids visible writes while MIA is actively
reading video RAM.

```asm
VIDX_VIDEO_STATUS     = $87
VIDEO_READOUT_ACTIVE = %00000100

wait_readout_clear:
    lda #VIDX_VIDEO_STATUS
    sta $FFE1

wait_loop:
    lda $FFE0
    and #VIDEO_READOUT_ACTIVE
    bne wait_loop
    rts

main_loop:
    jsr wait_readout_clear
    jsr read_input
    jsr update_game
    jsr draw_visible_video_state
    jmp main_loop
```

This avoids the most direct readout artifacts. It does not prevent repair chunks
from seeing newer visible values if packets are lost after the first send.

## Ack-Paced Mode

In ack-paced mode, the game avoids visible writes until the client has applied
and acknowledged the previous update.

```asm
VIDX_VIDEO_STATUS    = $87
VIDEO_UPDATE_ACTIVE = %00000010

wait_update_clear:
    lda #VIDX_VIDEO_STATUS
    sta $FFE1

wait_loop:
    lda $FFE0
    and #VIDEO_UPDATE_ACTIVE
    bne wait_loop
    rts

main_loop:
    jsr wait_update_clear
    jsr read_input
    jsr update_game
    jsr draw_visible_video_state
    jmp main_loop
```

This is the cleanest mode for visible updates. If Wi-Fi stalls or the client
stops acknowledging, the game can slow down or appear unresponsive. That is the
program's choice, similar to synchronizing tightly to a slow display device.

## Video Event IRQ

MIA can raise an IRQ when selected video events occur. This lets a program do
other work while waiting for a client update point.

Video event bits live in the video control block:

| Bit | Event | Meaning |
| ---: | --- | --- |
| 0 | `VIDEO_EVENT_FRAME_REQUEST` | MIA accepted a client update request |
| 1 | `VIDEO_EVENT_FRAME_SENT` | initial response send completed |
| 2 | `VIDEO_EVENT_FRAME_ACKED` | client acknowledged the response |
| 3 | `VIDEO_EVENT_CLIENT_CHANGE` | client connected or disconnected |
| 4 | `VIDEO_EVENT_FULL_REFRESH` | all pages were marked dirty |
| 5 | `VIDEO_EVENT_REPAIR_REQUEST` | client requested missing chunks |
| 6 | `VIDEO_EVENT_READOUT_START` | MIA started reading video RAM |
| 7 | `VIDEO_EVENT_READOUT_END` | MIA finished the current readout pass |

`VIDEO_IRQ_ENABLE` selects which event bits raise the video IRQ. Latched events
remain set until the program clears them by writing `1` bits to
`VIDEO_EVENT_STATUS`.

When an enabled video event is pending, MIA sets `IRQ_VIDEO_EVENT` (`$0020`) in
`IRQ_STATUS`. The normal `IRQ_MASK` register controls whether that source drives
the 6502 IRQ line.

For a clean client-paced loop, enable `VIDEO_EVENT_FRAME_ACKED`. For a lower
latency loop, enable `VIDEO_EVENT_FRAME_SENT` or `VIDEO_EVENT_READOUT_END`.

## Performance Tips

Small updates are cheap. Large memory changes are bandwidth-bound.

Good steady-state patterns:

- update scroll registers instead of rewriting nametables for camera movement;
- update OAM for sprites instead of rewriting background tiles;
- batch overlay text changes when possible;
- load CHR banks during setup or in inactive banks;
- avoid forcing full refreshes during gameplay.

Expensive patterns:

- rewriting all CHR banks every frame;
- clearing and rebuilding large nametable regions every frame;
- changing visible CHR data during readout;
- ack-pacing game logic over an unreliable network when responsiveness matters.

Build complete logical changes before the next clean point when possible. For
ack-paced loops, start visible writes after `VIDEO_EVENT_FRAME_ACKED`; for
readout-paced loops, start after `VIDEO_EVENT_READOUT_END`.

## Troubleshooting

- Client shows old graphics after connect: force a full refresh or reconnect the
  client.
- One-frame visual tearing: avoid visible writes while `VIDEO_READOUT_ACTIVE` is
  set.
- Repair-time artifacts: avoid visible writes while `VIDEO_UPDATE_ACTIVE` is
  set, or improve the Wi-Fi link.
- Slow game in ack-paced mode: the client/network is the limiter; use
  readout-paced or free-running mode if responsiveness matters more.
- Large updates miss 30 FPS: reduce dirty page count, lower client FPS, or avoid
  large CHR/nametable uploads during steady-state play.
