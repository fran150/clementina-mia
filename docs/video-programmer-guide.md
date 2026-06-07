# MIA Video Programmer Guide

This guide describes how a Clementina 6502 program uses MIA Wi-Fi video output.

## Mental Model

MIA is the 6502-facing half of a remote video system: it stores video state and
publishes changed memory pages via Wi-Fi, while a remote client maintains a
copy of the VRAM and does the rendering.

Your program writes tile maps, character graphics, palettes, sprites, scroll
values, and overlay text into MIA VRAM. MIA then synchronizes those changes to
the remote client on request.

The video region is 68,944 bytes. MIA divides it into 2,155 absolute pages of
32 bytes each. Page 0 (`$00000-$0001F`) is local control and diagnostic state
for MIA and the 6502; it is never sent to the client and writes there do not
dirty video state. The syncable client mirror starts at page 1 (`$00020`) and contains
2,154 pages. When your program writes a byte in the syncable region, the
containing page is marked changed. A later client request sends changed pages so
the client's mirror can catch up. Writing several bytes in the same page still
produces one changed page for the next update, while spreading changes over
many pages makes the update larger.

When the client requests an update, MIA snapshots the list of pages that changed
up to that point and starts building a response by reading those pages from live
MIA RAM. If your program changes visible memory while MIA is building that
response, the client may render unwanted artifacts because some bytes can be
read before the change and others after it. After MIA has sent the update, but
before the client acknowledges it, MIA may re-read those same pages if packets
were lost and the client requests repair. Artifacts are less likely in this
period, but visible writes can still appear in repair chunks. Once the client
acknowledges the update, any part of VRAM can be changed without artifact risk
until the next client request is accepted.

MIA exposes status flags and IRQ-capable video events for these lifecycle
points. Programs can ignore them and run freely, poll them when they need a
clean update point, or use IRQs to do other work while waiting. Those options
are described below.

This is similar to classic video hardware in one important way: if you change
visible video memory while the display system is reading it, you can get a
temporary artifact.

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

Video lifecycle status lives in the general `MIA_STATUS` register. Use bits
`MIA_STAT_VIDEO_FRAME_REQUESTED` and `MIA_STAT_VIDEO_FRAME_SENT` to decide
whether an update is being built, sent, repaired, or held for ACK.

Video lifecycle events use normal `IRQ_STATUS` bits. Enable the corresponding
bits in `IRQ_MASK` to let selected video events drive the 6502 IRQ line.

## Video Commands

Video commands use the normal command registers.

| Command | Id | Purpose |
| --- | ---: | --- |
| `VIDEO_ENABLE` | `$40` | initialize video state and fast video indexes |
| `VIDEO_FORCE_FULL_REFRESH` | `$42` | mark all syncable video pages dirty for the next client update |
| `VIDEO_SET_MODE` | `$43` | update `VIDEO_MODE` bits |

Code example of 6502 forcing a full refresh on the client:

```asm
VIDEO_FORCE_FULL_REFRESH = $42

video_force_full_refresh:
    lda #VIDEO_FORCE_FULL_REFRESH
    sta $FFE9       ; CMD_TRIGGER
    rts
```

A full refresh marks every syncable video page dirty, so the next accepted
update includes the whole client mirror. MIA sends the current contents of
those pages when the client requests that update. This can take much longer to
send than an ordinary dirty update. While that response is outstanding, new
writes are still tracked for the following update; if the program keeps
changing a lot of video state, the next update may also become large. After
`IRQ_VIDEO_FRAME_SENT`, read `LAST_RESPONSE_DIRTY_PAGES` to detect when MIA
sent a large update: a full refresh reports `2,154`, while ordinary gameplay
updates should usually be much smaller.

## Video Control Pages

The video control area starts at MIA RAM offset `$00000`. The full layout is in
[video-output.md](video-output.md).

Page 0 is local control and diagnostic state. MIA and the 6502 can read and
write this page, but the client never receives it and writes there do not mark
video pages dirty.

Important local fields:

| Offset | Field | Meaning |
| ---: | --- | --- |
| `$00` | `VIDEO_VERSION` | video state layout version |
| `$04-$07` | `FRAME_ID` | latest assigned client update id |
| `$08-$09` | `LAST_RESPONSE_DIRTY_PAGES` | dirty page count for the latest stable update result |

Page 1 is syncable render control state. The client mirrors this page and uses
it while rendering.

Important render fields:

| Offset | Field | Meaning |
| ---: | --- | --- |
| `$20` | `VIDEO_MODE` | video enable and renderer mode bits |
| `$21` | `LAYER_ENABLE` | background, overlay, sprite enables |
| `$22-$23` | `SCROLL_X` | background scroll X |
| `$24-$25` | `SCROLL_Y` | background scroll Y |
| `$26` | `BG_ACTIVE_SET` | active 2x2 background set |
| `$27` | `BG_SCROLL_MODE` | background plane mode |
| `$28-$2C` | bank selectors | bg, bg alt, overlay, overlay alt, sprite |
| `$2E-$2F` | `OAM_ACTIVE_COUNT` | number of OAM records the renderer evaluates |

`OAM_ACTIVE_COUNT` limits sprite evaluation to the first N OAM records. Any
record outside that range is ignored even if its per-sprite `DISABLE` bit is
clear.

`FRAME_ID` starts at `0` for a session, increments when MIA accepts a client
request that produces an update, and wraps from `0xFFFFFFFF` to `1`.

`LAST_RESPONSE_DIRTY_PAGES` is `0` after reset or when the latest stable update
result was `STATUS(NO_DIRTY_PAGES)`. For an update that sends video data, the
field becomes stable after the first complete response send finishes, just
before `IRQ_VIDEO_FRAME_SENT` is published. While MIA is still building or
sending that response, the field still contains the previous stable value. Use
it with `MIA_STATUS` or video IRQ events to notice when Wi-Fi or a full refresh
has made updates unusually large.

General `MIA_STATUS` video lifecycle bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 5 | `MIA_STAT_VIDEO_FRAME_REQUESTED` | MIA accepted an update request; ACK not received yet |
| 6 | `MIA_STAT_VIDEO_FRAME_SENT` | initial response send finished; ACK may still be pending |

The clean-output rule is:

```text
do not change visible memory while either MIA_STATUS video lifecycle bit is set
```

That waits until the client has acknowledged the update, so later repair chunks
cannot pick up newer visible values.

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
every syncable video page dirty. Large startup writes affect startup transfer
time, not steady-state bandwidth.

## Update Lifecycle

MIA and the client loop through this lifecycle:

```text
all clear
client requests update
MIA accepts the request and starts building the response
MIA sends dirty page records
MIA finishes first send
client requests missing chunks if needed
client applies the complete update
client ACKs the frame
MIA reports ACK and returns to all clear
```

If no syncable video pages changed, MIA returns `STATUS(NO_DIRTY_PAGES)`. That
does not assign a new `FRAME_ID`, does not require an ACK, and leaves the video
lifecycle all clear.

During an update that sends data:

- both `MIA_STATUS` video lifecycle bits clear means all clear; no update is
  being sent, repaired, or held for acknowledgement.
- `IRQ_VIDEO_FRAME_REQUEST` is set when MIA accepts the request.
- `MIA_STAT_VIDEO_FRAME_REQUESTED` is set from accepted request until ACK.
- `IRQ_VIDEO_FRAME_SENT` is set when the first complete send has finished; at
  this point `LAST_RESPONSE_DIRTY_PAGES` is stable for that response.
- `MIA_STAT_VIDEO_FRAME_SENT` is set from first complete send until ACK.
- `IRQ_VIDEO_FRAME_ACKED` is set after the client acknowledges the update and
  MIA has returned to the all-clear state.

Changing visible memory at different points has different tradeoffs:

| When you write visible memory | Result |
| --- | --- |
| while all clear, before a request is accepted | included cleanly in the next update |
| while `MIA_STAT_VIDEO_FRAME_REQUESTED` is set and `MIA_STAT_VIDEO_FRAME_SENT` is clear | may appear partially in the current update |
| while both status bits are set | may appear in repair chunks for that update |
| after ACK, when all clear again | belongs cleanly to a later update |

Writes to inactive or non-visible resources are safe as long as they cannot
affect the update currently being sent. For example, loading an unused CHR bank
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

The program keeps its own pace. MIA keeps marking changed pages. If the client
or network falls behind, the next accepted response includes the accumulated
changed pages. The client may see transient artifacts if visible memory changes
while MIA is reading it, but the mirror converges on later updates.

Use this mode when local responsiveness matters more than remote display
cleanliness.

## Ack-Paced Mode

In ack-paced mode, the game avoids visible writes until the client has applied
and acknowledged the previous update.

```asm
MIA_STATUS_L         = $FFEA
MIA_STAT_VIDEO_BUSY  = %01100000

wait_update_clear:
wait_loop:
    lda MIA_STATUS_L
    and #MIA_STAT_VIDEO_BUSY
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

## Video IRQ Events

MIA can raise an IRQ when selected video events occur. This lets a program do
other work while waiting for a client update point.

Video event bits live in the normal `IRQ_STATUS` register:

| Bit | Event | Meaning |
| ---: | --- | --- |
| 5 | `IRQ_VIDEO_FRAME_REQUEST` | MIA accepted a client update request |
| 6 | `IRQ_VIDEO_FRAME_SENT` | initial response send completed |
| 7 | `IRQ_VIDEO_FRAME_ACKED` | client acknowledged the response |

`IRQ_MASK` selects which pending event bits raise the physical IRQ line. Pending
video event bits are cleared through the normal `IRQ_STATUS` mechanism, the same
as other MIA IRQ sources.

For a clean client-paced loop, enable `IRQ_VIDEO_FRAME_ACKED`. For a lower
latency loop, enable `IRQ_VIDEO_FRAME_SENT` and accept possible repair-time
artifacts.

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
- changing visible CHR data while an update is outstanding;
- ack-pacing game logic over an unreliable network when responsiveness matters.

Build complete logical changes before the next clean point when possible. For
ack-paced loops, start visible writes after `IRQ_VIDEO_FRAME_ACKED`.

## Troubleshooting

- Client shows old graphics after connect: force a full refresh or reconnect the
  client.
- One-frame visual tearing: avoid visible writes while either `MIA_STATUS`
  video lifecycle bit is set.
- Repair-time artifacts: wait for `IRQ_VIDEO_FRAME_ACKED` before visible
  writes, or improve the Wi-Fi link.
- Slow game in ack-paced mode: the client/network is the limiter; use
  free-running mode if responsiveness matters more.
- Large updates miss 30 FPS: reduce dirty page count, lower client FPS, or avoid
  large CHR/nametable uploads during steady-state play.
