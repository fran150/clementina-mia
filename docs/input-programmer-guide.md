# MIA Input Programmer Guide

This guide describes how 6502 programs read MIA input. The complete subsystem
definition is in [input.md](input.md), and the Wi-Fi protocol is in
[input-protocol.md](input-protocol.md).

## Constants

```asm
IDXA_PORT          = $FFE0
IDXA_SELECT        = $FFE1
IDXB_PORT          = $FFE4
IDXB_SELECT        = $FFE5
CMD_PARAM1         = $FFE6
CMD_PARAM2         = $FFE7
CMD_PARAM3         = $FFE8
CMD_TRIGGER        = $FFE9
IRQ_MASK_L         = $FFEE
IRQ_MASK_H         = $FFEF
IRQ_STATUS_L       = $FFF0
IRQ_STATUS_H       = $FFF1

INPUT_STATUS       = $FFF2
INPUT_CHAR         = $FFF3
INPUT_CHAR_COUNT   = $FFF4

INPUT_TEXT_READY       = %00000001
INPUT_KEYBOARD_DOWN    = %00000010
INPUT_CONSUMER_DOWN    = %00000100
INPUT_MOUSE_DOWN       = %00001000
INPUT_GAMEPAD_DOWN     = %00010000
INPUT_SOURCE_CONSOLE   = %00100000
INPUT_SOURCE_WIFI      = %01000000
INPUT_SOURCE_USB_HOST  = %10000000

IRQ_INPUT_KEYBOARD = $0100
IRQ_INPUT_MOUSE    = $0200
IRQ_INPUT_GAMEPAD  = $0400

INPUT_MODE_CONSOLE  = $00
INPUT_MODE_WIFI     = $01
INPUT_MODE_USB_HOST = $02

CMD_INPUT_SET_MODE = $50
CMD_INPUT_SET_PROBE = $51

IIDX_KEYBOARD_PROBE_0 = $50  ; through $57
IIDX_CONSUMER_PROBE_0 = $58  ; through $5F
IIDX_INPUT_STATE      = $60
IIDX_KEYBOARD_BITMAP = $61
IIDX_CONSUMER_BITMAP = $62
IIDX_MOUSE_STATE     = $63
IIDX_GAMEPAD_0       = $64
IIDX_GAMEPAD_1       = $65
IIDX_GAMEPAD_2       = $66
IIDX_GAMEPAD_3       = $67
IIDX_INPUT_CONTROL   = $68

INPUT_DEVICE_FLAGS_OFS   = 0
KEYBOARD_EVENT_FLAGS_OFS = 1
KEYBOARD_EVENT_MASK_OFS  = 2
KEYBOARD_EVENT_ACK_OFS   = 3
MOUSE_EVENT_FLAGS_OFS    = 4
MOUSE_EVENT_MASK_OFS     = 5
MOUSE_EVENT_ACK_OFS      = 6
GAMEPAD_EVENT_FLAGS_OFS  = 7
GAMEPAD_EVENT_MASK_OFS   = 8
GAMEPAD_EVENT_ACK_OFS    = 9

INPUT_DEVICE_KEYBOARD = %00000001
INPUT_DEVICE_CONSUMER = %00000010
INPUT_DEVICE_MOUSE    = %00000100
INPUT_DEVICE_PAD0     = %00001000
INPUT_DEVICE_PAD1     = %00010000
INPUT_DEVICE_PAD2     = %00100000
INPUT_DEVICE_PAD3     = %01000000

KEY_EVENT_TEXT          = %00000001
KEY_EVENT_KEY_DOWN      = %00000010
KEY_EVENT_KEY_UP        = %00000100
KEY_EVENT_CONSUMER_DOWN = %00001000
KEY_EVENT_CONSUMER_UP   = %00010000
KEY_EVENT_DEVICE        = %00100000

MOUSE_EVENT_BUTTON_DOWN = %00000001
MOUSE_EVENT_BUTTON_UP   = %00000010
MOUSE_EVENT_MOVE        = %00000100
MOUSE_EVENT_SCROLL      = %00001000
MOUSE_EVENT_DEVICE      = %00010000

GAMEPAD_EVENT_BUTTON_DOWN = %00000001
GAMEPAD_EVENT_BUTTON_UP   = %00000010
GAMEPAD_EVENT_DPAD        = %00000100
GAMEPAD_EVENT_STICK       = %00001000
GAMEPAD_EVENT_TRIGGER     = %00010000
GAMEPAD_EVENT_DEVICE      = %00100000
```

## Register Summary

| Address | Name | Use |
| ---: | --- | --- |
| `$FFF2` | `INPUT_STATUS` | Check text availability, held digital input, and active source. |
| `$FFF3` | `INPUT_CHAR` | Read one text byte and remove it from the FIFO. |
| `$FFF4` | `INPUT_CHAR_COUNT` | Read the number of queued text bytes. |
| `$FFF5-$FFF9` | reserved | Reads return zero. Writes are ignored. |

## Status Flags

`INPUT_STATUS` bits:

| Bit | Constant | Meaning |
| ---: | --- | --- |
| 0 | `INPUT_TEXT_READY` | At least one text byte is queued. |
| 1 | `INPUT_KEYBOARD_DOWN` | At least one represented Keyboard/Keypad usage is held. |
| 2 | `INPUT_CONSUMER_DOWN` | At least one represented Consumer usage is held. |
| 3 | `INPUT_MOUSE_DOWN` | At least one mouse button is held. |
| 4 | `INPUT_GAMEPAD_DOWN` | At least one gamepad digital control is held. |
| 5 | `INPUT_SOURCE_CONSOLE` | Console input owns the input subsystem. |
| 6 | `INPUT_SOURCE_WIFI` | Wi-Fi input owns the input subsystem. |
| 7 | `INPUT_SOURCE_USB_HOST` | USB host input owns the input subsystem. |

`INPUT_TEXT_READY` is equivalent to `INPUT_CHAR_COUNT != 0`.
`INPUT_MOUSE_DOWN` is equivalent to `MOUSE_STATE.buttons & $1F != 0`.
`INPUT_GAMEPAD_DOWN` is set when any reported gamepad has an active d-pad bit,
digital stick summary bit, or digital button bit. Analog stick and trigger
movement does not affect `INPUT_GAMEPAD_DOWN`.
Device availability is reported by `INPUT_DEVICE_FLAGS` in the input control
block.

## Changing Input Mode

Input mode changes use `CMD_INPUT_SET_MODE`.

Command registers:

| Address | Name | Use |
| ---: | --- | --- |
| `$FFE6` | `CMD_PARAM1` | Mode value. |
| `$FFE7` | `CMD_PARAM2` | Reserved, write zero. |
| `$FFE8` | `CMD_PARAM3` | Reserved, write zero. |
| `$FFE9` | `CMD_TRIGGER` | Write `CMD_INPUT_SET_MODE` (`$50`) to request the mode change. |

Mode values:

| Value | Constant | Meaning |
| ---: | --- | --- |
| `$00` | `INPUT_MODE_CONSOLE` | Console text input mode. |
| `$01` | `INPUT_MODE_WIFI` | Wi-Fi input mode. |
| `$02` | `INPUT_MODE_USB_HOST` | USB host input mode. |

If the requested mode is not available in the current build, MIA leaves the
active mode unchanged. Read `INPUT_STATUS` after command completion to confirm
which mode is active.

`CMD_INPUT_SET_MODE` is available in both `MIA_USB_MODE=device` and
`MIA_USB_MODE=host` builds. In USB-device builds, the terminal console commands
`input console` and `input wifi` are also available.

In a `MIA_USB_MODE=host` build, MIA boots in `INPUT_MODE_USB_HOST` and the Pico
USB port is not available as a terminal console. A 6502 program can change the
active input mode at any point by issuing `CMD_INPUT_SET_MODE`. USB HID device
decoding is reserved for the TinyUSB host integration; current USB-host builds
report the USB host source without device state.

```asm
enable_wifi_input:
    lda #INPUT_MODE_WIFI
    sta CMD_PARAM1
    lda #$00
    sta CMD_PARAM2
    sta CMD_PARAM3
    lda #CMD_INPUT_SET_MODE
    sta CMD_TRIGGER

    ; Poll status or wait for IRQ_COMMAND, then confirm the active source.
    lda INPUT_STATUS
    and #INPUT_SOURCE_WIFI
    bne wifi_active
    rts

wifi_active:
    ; Wi-Fi input listener is active on UDP port 6503.
    rts
```

## Positioning Probe Indexes

`CMD_INPUT_SET_PROBE` positions one of the fixed one-byte probe indexes on a
Keyboard/Keypad or Consumer bitmap byte.

Command registers:

| Address | Name | Use |
| ---: | --- | --- |
| `$FFE6` | `CMD_PARAM1` | Probe id. `$00-$07` are keyboard probes, `$08-$0F` are consumer probes. |
| `$FFE7` | `CMD_PARAM2` | Bitmap byte offset, masked with `$1F`. |
| `$FFE8` | `CMD_PARAM3` | Reserved, write zero. |
| `$FFE9` | `CMD_TRIGGER` | Write `CMD_INPUT_SET_PROBE` (`$51`) to position the probe. |

Example: park `IIDX_KEYBOARD_PROBE_1` on keyboard bitmap byte `$1C`, which
contains Keyboard/Keypad usages `$E0-$E7`:

```asm
    lda #$01             ; keyboard probe 1
    sta CMD_PARAM1
    lda #$1C
    sta CMD_PARAM2
    lda #$00
    sta CMD_PARAM3
    lda #CMD_INPUT_SET_PROBE
    sta CMD_TRIGGER
```

## Reading Text

`INPUT_CHAR` is a read-to-pop FIFO port. Check `INPUT_TEXT_READY` before
reading, or read `INPUT_CHAR_COUNT`.

```asm
read_one_char:
    lda INPUT_STATUS
    and #INPUT_TEXT_READY
    beq no_char

    lda INPUT_CHAR        ; A = next PETSCII-compatible text byte
    jsr handle_char

no_char:
    rts
```

To drain everything currently queued:

```asm
drain_text:
    lda INPUT_CHAR_COUNT
    beq done
    tax

loop:
    lda INPUT_CHAR
    jsr handle_char
    dex
    bne loop

done:
    rts
```

The FIFO stores 64 bytes. When it is full, MIA drops the oldest byte and queues
the new byte.

## Keyboard and Consumer Bitmaps

Keyboard/Keypad page `$0007` and Consumer page `$000C` each have a fixed
32-byte bitmap. Keyboard/Keypad contains normal keys, modifiers, arrows,
function keys, and the numeric keypad. Consumer contains media keys and similar
controls.

## Testing Known Keys with Probe Indexes

For HID usage ID `N`:

```text
byte = N >> 3
mask = 1 << (N & 7)
```

MIA reserves eight one-byte Keyboard/Keypad probe indexes and eight one-byte
Consumer probe indexes:

| Index Range | Name | Default Address | Use |
| ---: | --- | ---: | --- |
| `$50-$57` | `IIDX_KEYBOARD_PROBE_0-7` | `$11000` | one-byte Keyboard/Keypad probes |
| `$58-$5F` | `IIDX_CONSUMER_PROBE_0-7` | `$11020` | one-byte Consumer probes |

The probe indexes start at byte 0 of their bitmap with read/write stepping
disabled. Use `CMD_INPUT_SET_PROBE` to position a probe at the bitmap byte you
want to poll. Repeated reads through the selected index then return that same
bitmap byte.

HID Keyboard/Keypad usage `$04` is `A`. It is stored in keyboard bitmap byte
`$00`, bit 4. A keyboard probe parked at `$11000` can test it:

```asm
KEY_A_MASK = %00010000

is_a_down:
    lda #IIDX_KEYBOARD_PROBE_0
    sta IDXA_SELECT
    lda IDXA_PORT
    and #KEY_A_MASK
    rts                 ; Z clear means A is held
```

HID Keyboard/Keypad usage `$E1` is left shift. It is stored in byte `$1C`, bit
1. Park keyboard probe 1 at byte `$1C` before reading it:

```asm
KEY_LSHIFT_MASK = %00000010

park_left_shift_probe:
    lda #$01             ; keyboard probe 1
    sta CMD_PARAM1
    lda #$1C
    sta CMD_PARAM2
    lda #$00
    sta CMD_PARAM3
    lda #CMD_INPUT_SET_PROBE
    sta CMD_TRIGGER
    rts

is_left_shift_down:
    lda #IIDX_KEYBOARD_PROBE_1
    sta IDXA_SELECT
    lda IDXA_PORT
    and #KEY_LSHIFT_MASK
    rts
```

Consumer usage `$E9` is volume increment. It is stored in byte `$1D`, bit 1.
Park consumer probe 0 at byte `$1D` before reading it:

```asm
CONS_VOL_UP_MASK = %00000010

park_volume_probe:
    lda #$08             ; consumer probe 0
    sta CMD_PARAM1
    lda #$1D
    sta CMD_PARAM2
    lda #$00
    sta CMD_PARAM3
    lda #CMD_INPUT_SET_PROBE
    sta CMD_TRIGGER
    rts

is_volume_up_down:
    lda #IIDX_CONSUMER_PROBE_0
    sta IDXA_SELECT
    lda IDXA_PORT
    and #CONS_VOL_UP_MASK
    rts
```

## Reading Full HID Bitmaps

Programs that need several keys, key binding, or press/release edge detection
can stream the full 32-byte keyboard bitmap through `IIDX_KEYBOARD_BITMAP` and
the full 32-byte consumer bitmap through `IIDX_CONSUMER_BITMAP`.

```asm
bitmap_buffer = $0200

read_keyboard_bitmap:
    lda #IIDX_KEYBOARD_BITMAP
    sta IDXA_SELECT

    ldx #$00
loop:
    lda IDXA_PORT
    sta bitmap_buffer,x
    inx
    cpx #32
    bne loop
    rts
```

To detect edges, keep a previous copy and compare it to the current copy:

```text
pressed  = current & ~previous
released = previous & ~current
held     = current
```

The bitmap does not store transition order. Programs that need combinations or
ordering maintain that state locally.

## Input Control Block

`IIDX_INPUT_CONTROL` streams the 11-byte input control block at
`$11045-$1104F`.

| Offset | Name | Use |
| ---: | --- | --- |
| 0 | `INPUT_DEVICE_FLAGS` | active-source capabilities and reported gamepad slots |
| 1 | `KEYBOARD_EVENT_FLAGS` | latched keyboard, consumer, and text event causes |
| 2 | `KEYBOARD_EVENT_MASK` | event bits that can raise `IRQ_INPUT_KEYBOARD` |
| 3 | `KEYBOARD_EVENT_ACK` | write `1` bits to clear `KEYBOARD_EVENT_FLAGS` |
| 4 | `MOUSE_EVENT_FLAGS` | latched mouse event causes |
| 5 | `MOUSE_EVENT_MASK` | event bits that can raise `IRQ_INPUT_MOUSE` |
| 6 | `MOUSE_EVENT_ACK` | write `1` bits to clear `MOUSE_EVENT_FLAGS` |
| 7 | `GAMEPAD_EVENT_FLAGS` | latched gamepad event causes |
| 8 | `GAMEPAD_EVENT_MASK` | event bits that can raise `IRQ_INPUT_GAMEPAD` |
| 9 | `GAMEPAD_EVENT_ACK` | write `1` bits to clear `GAMEPAD_EVENT_FLAGS` |
| 10 | reserved | reads as zero |

`INPUT_DEVICE_FLAGS` bits:

| Bit | Constant | Meaning |
| ---: | --- | --- |
| 0 | `INPUT_DEVICE_KEYBOARD` | Keyboard/Keypad state is available. |
| 1 | `INPUT_DEVICE_CONSUMER` | Consumer control state is available. |
| 2 | `INPUT_DEVICE_MOUSE` | Mouse state is available. |
| 3 | `INPUT_DEVICE_PAD0` | Gamepad slot 0 is reported connected. |
| 4 | `INPUT_DEVICE_PAD1` | Gamepad slot 1 is reported connected. |
| 5 | `INPUT_DEVICE_PAD2` | Gamepad slot 2 is reported connected. |
| 6 | `INPUT_DEVICE_PAD3` | Gamepad slot 3 is reported connected. |
| 7 | reserved | Reads as zero. |

Read the control block when a program needs device availability or detailed IRQ
causes:

```asm
input_control = $0240

read_input_control:
    lda #IIDX_INPUT_CONTROL
    sta IDXA_SELECT

    ldx #$00
loop:
    lda IDXA_PORT
    sta input_control,x
    inx
    cpx #11
    bne loop
    rts
```

## Input IRQ Events

Input uses three ordinary MIA IRQ bits:

| IRQ Bit | Constant | Meaning |
| ---: | --- | --- |
| 8 | `IRQ_INPUT_KEYBOARD` | Keyboard, Consumer, or text event. |
| 9 | `IRQ_INPUT_MOUSE` | Mouse event. |
| 10 | `IRQ_INPUT_GAMEPAD` | Gamepad event. |

Enable the top-level input IRQ bits through `IRQ_MASK`, the same way as other
MIA IRQ sources. Fine-grained enable bits live in the input control block:

| Event Byte | Mask Byte | ACK Byte | IRQ Bit |
| --- | --- | --- | --- |
| `KEYBOARD_EVENT_FLAGS` | `KEYBOARD_EVENT_MASK` | `KEYBOARD_EVENT_ACK` | `IRQ_INPUT_KEYBOARD` |
| `MOUSE_EVENT_FLAGS` | `MOUSE_EVENT_MASK` | `MOUSE_EVENT_ACK` | `IRQ_INPUT_MOUSE` |
| `GAMEPAD_EVENT_FLAGS` | `GAMEPAD_EVENT_MASK` | `GAMEPAD_EVENT_ACK` | `IRQ_INPUT_GAMEPAD` |

MIA sets event flag bits when the matching input event occurs. An event group
can raise its top-level IRQ when `EVENT_FLAGS & EVENT_MASK` is nonzero. Reading
`IRQ_STATUS_L` at `$FFF0` clears the top-level IRQ bits, but does not clear the
detailed event flags. Clear detailed flags by writing `1` bits to the matching
ACK byte.

Keyboard event bits:

| Bit | Constant | Meaning |
| ---: | --- | --- |
| 0 | `KEY_EVENT_TEXT` | One or more text bytes were queued. |
| 1 | `KEY_EVENT_KEY_DOWN` | One or more Keyboard/Keypad usages changed from up to down. |
| 2 | `KEY_EVENT_KEY_UP` | One or more Keyboard/Keypad usages changed from down to up. |
| 3 | `KEY_EVENT_CONSUMER_DOWN` | One or more Consumer usages changed from up to down. |
| 4 | `KEY_EVENT_CONSUMER_UP` | One or more Consumer usages changed from down to up. |
| 5 | `KEY_EVENT_DEVICE` | Keyboard or Consumer availability changed. |

Mouse event bits:

| Bit | Constant | Meaning |
| ---: | --- | --- |
| 0 | `MOUSE_EVENT_BUTTON_DOWN` | One or more mouse buttons changed from up to down. |
| 1 | `MOUSE_EVENT_BUTTON_UP` | One or more mouse buttons changed from down to up. |
| 2 | `MOUSE_EVENT_MOVE` | `x` or `y` accumulator changed. |
| 3 | `MOUSE_EVENT_SCROLL` | `wheel` or `pan` accumulator changed. |
| 4 | `MOUSE_EVENT_DEVICE` | Mouse availability changed. |

Gamepad event bits:

| Bit | Constant | Meaning |
| ---: | --- | --- |
| 0 | `GAMEPAD_EVENT_BUTTON_DOWN` | One or more gamepad button bits changed from up to down. |
| 1 | `GAMEPAD_EVENT_BUTTON_UP` | One or more gamepad button bits changed from down to up. |
| 2 | `GAMEPAD_EVENT_DPAD` | A gamepad d-pad bit changed. |
| 3 | `GAMEPAD_EVENT_STICK` | A gamepad stick value or digital stick summary changed. |
| 4 | `GAMEPAD_EVENT_TRIGGER` | A gamepad analog trigger changed. |
| 5 | `GAMEPAD_EVENT_DEVICE` | A gamepad slot connected or disconnected. |

To enable interrupts for text, key press, and key release:

```asm
    lda IRQ_MASK_H
    ora #%00000001      ; enable IRQ bit 8, IRQ_INPUT_KEYBOARD
    sta IRQ_MASK_H

    lda #IIDX_INPUT_CONTROL
    sta IDXA_SELECT

    ; Skip INPUT_DEVICE_FLAGS and KEYBOARD_EVENT_FLAGS.
    lda IDXA_PORT
    lda IDXA_PORT

    lda #(KEY_EVENT_TEXT | KEY_EVENT_KEY_DOWN | KEY_EVENT_KEY_UP)
    sta IDXA_PORT        ; KEYBOARD_EVENT_MASK
```

An IRQ handler reads `IRQ_STATUS`, samples the detailed event flags, handles the
matching state, and acknowledges the detailed flags:

```asm
handle_input_keyboard_irq:
    jsr read_input_control

    lda input_control+KEYBOARD_EVENT_FLAGS_OFS
    sta handled_key_events

    ; Drain text or read bitmaps here.

    lda handled_key_events
    sta input_control+KEYBOARD_EVENT_ACK_OFS

    lda #IIDX_INPUT_CONTROL
    sta IDXA_SELECT

    ldx #KEYBOARD_EVENT_ACK_OFS
skip_to_ack:
    lda IDXA_PORT
    dex
    bne skip_to_ack

    lda handled_key_events
    sta IDXA_PORT        ; write ACK bits
    rts
```

## Reading Mouse State

`IIDX_MOUSE_STATE` streams five bytes:

| Offset | Name |
| ---: | --- |
| 0 | `buttons` |
| 1 | `x` |
| 2 | `y` |
| 3 | `wheel` |
| 4 | `pan` |

```asm
mouse_state = $0220

read_mouse:
    lda #IIDX_MOUSE_STATE
    sta IDXA_SELECT

    ldx #$00
loop:
    lda IDXA_PORT
    sta mouse_state,x
    inx
    cpx #5
    bne loop
    rts
```

Mouse buttons:

| Bit | Button |
| ---: | --- |
| 0 | Left |
| 1 | Right |
| 2 | Middle |
| 3 | Backward |
| 4 | Forward |

`x`, `y`, `wheel`, and `pan` are wrapping accumulators. Store the previous
sample and subtract it from the current sample:

```asm
; A = current mouse x accumulator
; prev_mouse_x holds previous accumulator
    sec
    sbc prev_mouse_x
    sta mouse_dx        ; signed 8-bit delta
```

Poll mouse movement often enough that the signed delta between samples stays in
the `-128..127` range.

## Reading Gamepad State

Each gamepad slot is 10 bytes. Use `IIDX_GAMEPAD_0` through
`IIDX_GAMEPAD_3`.

```asm
pad0_state = $0230

read_gamepad0:
    lda #IIDX_GAMEPAD_0
    sta IDXA_SELECT

    ldx #$00
loop:
    lda IDXA_PORT
    sta pad0_state,x
    inx
    cpx #10
    bne loop
    rts
```

Slot layout:

| Offset | Name | Meaning |
| ---: | --- | --- |
| 0 | `dpad` | digital d-pad and connected/type flags |
| 1 | `sticks` | digital summaries for left and right sticks |
| 2 | `button0` | face and shoulder buttons |
| 3 | `button1` | triggers, select/start, home, stick buttons |
| 4 | `lx` | left stick X, `-128..127` |
| 5 | `ly` | left stick Y, `-128..127` |
| 6 | `rx` | right stick X, `-128..127` |
| 7 | `ry` | right stick Y, `-128..127` |
| 8 | `lt` | left trigger, `0..255` |
| 9 | `rt` | right trigger, `0..255` |

Check `dpad` bit 7 before using a slot:

```asm
PAD_CONNECTED = %10000000
PAD_UP        = %00000001
PAD_DOWN      = %00000010
PAD_LEFT      = %00000100
PAD_RIGHT     = %00001000

use_pad0:
    lda pad0_state+0
    and #PAD_CONNECTED
    beq no_pad

    lda pad0_state+0
    and #PAD_LEFT
    bne move_left

no_pad:
    rts
```

`button0` bits:

| Bit | Meaning |
| ---: | --- |
| 0 | A or Cross |
| 1 | B or Circle |
| 2 | C or Right Paddle |
| 3 | X or Square |
| 4 | Y or Triangle |
| 5 | Z or Left Paddle |
| 6 | L1 |
| 7 | R1 |

`button1` bits:

| Bit | Meaning |
| ---: | --- |
| 0 | L2 |
| 1 | R2 |
| 2 | Select or Back |
| 3 | Start or Menu |
| 4 | Home |
| 5 | L3 |
| 6 | R3 |
| 7 | reserved |

## Source and Device Status

`INPUT_STATUS` identifies the active source and fast held-state summaries.
`INPUT_DEVICE_FLAGS` in the input control block identifies available source
capabilities and reported gamepad slots.

Input mode is selected with `CMD_INPUT_SET_MODE`, the terminal console command
`input console`, the terminal console command `input wifi`, or the firmware's
build-time input defaults.

```asm
input_from_wifi:
    lda INPUT_STATUS
    and #INPUT_SOURCE_WIFI
    rts

has_gamepad:
    jsr read_input_control
    lda input_control+INPUT_DEVICE_FLAGS_OFS
    and #(INPUT_DEVICE_PAD0 | INPUT_DEVICE_PAD1 | INPUT_DEVICE_PAD2 | INPUT_DEVICE_PAD3)
    rts
```

Device flags are summaries. Device-specific state remains the authority; for
example, each gamepad slot's `dpad` bit 7 identifies whether that slot is
connected.

## Polling Pattern for Games

A typical game loop:

1. Select keyboard bitmap and read the bytes for keys the game cares about.
2. Read `INPUT_DEVICE_FLAGS` if the game needs to skip unavailable devices.
3. Read gamepad slot 0 if the matching device flag or slot connected bit is set.
4. Read mouse state if `INPUT_DEVICE_MOUSE` is set.
5. Compare current samples to previous samples to derive press and release edges.
6. Drain text FIFO only if the game accepts typed text.

For movement controls, prefer held-state from the bitmap or gamepad fields. For
menus and text entry, use press edges or the text FIFO.
