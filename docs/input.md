# MIA Input

This document defines MIA's input subsystem. The Wi-Fi input wire protocol is
defined in [input-protocol.md](input-protocol.md), and the 6502 programming
model is defined in [input-programmer-guide.md](input-programmer-guide.md).

MIA exposes input in three forms:

- A text FIFO for programs that consume characters.
- Fixed 32-byte HID bitmaps for digital keyboard and consumer controls.
- Fixed mouse and gamepad state structures for pointer and controller input.

The 6502 interface is the same regardless of whether the active input mode uses
the terminal console, Wi-Fi input client, or USB HID host. The console mode only
produces text input. Wi-Fi mode can update text, keyboard, consumer, mouse, and
gamepad state. USB host mode is selectable in USB-host builds; USB HID device
decoding is reserved for the TinyUSB host integration.

## Input Sources

Only one input mode owns the live input state at a time.

| Mode | Activation | State Supported | Release |
| --- | --- | --- | --- |
| Console | `input console` in the Pico terminal console, or USB-device build default `MIA_INPUT_DEFAULT_MODE=console` | text FIFO | `Ctrl+Q` |
| Wi-Fi | `input wifi` in the Pico terminal console, or USB-device build default `MIA_INPUT_DEFAULT_MODE=wifi` | text FIFO, keyboard, consumer, mouse, gamepads | disconnect packet, replacement `HELLO`, terminal `input` command, or `CMD_INPUT_SET_MODE` |
| USB host | USB-host build `MIA_USB_MODE=host` | reserved for USB HID host input | source shutdown |

When a mode is released, MIA clears live held-state and device-state fields for
that mode. Bytes already queued in the text FIFO remain available to the 6502
program.

### Input Mode Selection

`MIA_USB_MODE` selects the USB role at build time. The default role is `device`,
which keeps the Pico USB port available for the terminal console. The `host`
role disables USB stdio and selects the USB host input source at boot. USB HID
device decoding is reserved for the TinyUSB host integration.

`MIA_INPUT_DEFAULT_MODE` selects the input mode entered at boot in a
`MIA_USB_MODE=device` build. Valid values are `console` and `wifi`. The default
is `console`.

The terminal console command `input console` enters console text input mode.
While this mode is active, terminal key presses are forwarded to the text FIFO
until the user presses `Ctrl+Q`, which returns the terminal to normal command
mode.

The terminal console command `input wifi` enables the Wi-Fi input listener on
UDP port `6503`. A Wi-Fi input client owns the Wi-Fi input session until it
disconnects, another client sends an accepted `HELLO`, or the active input mode
changes. The terminal console remains available for commands while Wi-Fi input
mode is active.

In both `MIA_USB_MODE=device` and `MIA_USB_MODE=host` builds, the 6502 can
change the active input mode at any point with the `CMD_INPUT_SET_MODE`
command. In USB-device builds, the terminal console commands provide an
additional way for the user to select console or Wi-Fi input.

In a `MIA_USB_MODE=host` build, `INPUT_MODE_USB_HOST` is the boot input mode.
The Pico USB port is used as a host port instead of a USB stdio terminal, so
terminal console commands are not available in this mode.

## Input Commands

Input mode changes use the normal MIA command registers:

```text
CMD_PARAM1  $FFE6
CMD_PARAM2  $FFE7
CMD_PARAM3  $FFE8
CMD_TRIGGER $FFE9
```

| Command | Parameters | Description |
| ---: | --- | --- |
| `$50` | `p1 = mode`, `p2 = 0`, `p3 = 0` | `CMD_INPUT_SET_MODE`: request an input mode change. |
| `$51` | `p1 = probe`, `p2 = byte_offset`, `p3 = 0` | `CMD_INPUT_SET_PROBE`: position a one-byte keyboard or consumer probe. |

`CMD_INPUT_SET_MODE` mode values:

| Value | Name | Meaning |
| ---: | --- | --- |
| `$00` | `INPUT_MODE_CONSOLE` | Console text input mode. |
| `$01` | `INPUT_MODE_WIFI` | Wi-Fi input mode. |
| `$02` | `INPUT_MODE_USB_HOST` | USB host input mode. |
| `$03-$FF` | reserved | Ignored. |

If the requested mode is not available in the current build, MIA leaves the
active mode unchanged. `INPUT_STATUS` reports the mode that is actually active.

`CMD_INPUT_SET_PROBE` probe values:

| Value | Probe |
| ---: | --- |
| `$00-$07` | `IIDX_KEYBOARD_PROBE_0-7` |
| `$08-$0F` | `IIDX_CONSUMER_PROBE_0-7` |
| `$10-$FF` | ignored |

`byte_offset` is masked with `$1F`. Keyboard probes are positioned at
`KEYBOARD_BITMAP + byte_offset`; consumer probes are positioned at
`CONSUMER_BITMAP + byte_offset`. Probe indexes remain one byte long with
stepping disabled.

## CPU Register Map

MIA exposes the input control registers in the reserved register range
`$FFF2-$FFF4`.

| Register | Address | Name | Access | Description |
| ---: | ---: | --- | --- | --- |
| `12` | `$FFF2` | `INPUT_STATUS` | read-only | Text availability, held digital input, and active-source flags. |
| `13` | `$FFF3` | `INPUT_CHAR` | read-to-pop | Text FIFO read port. Reading pops one byte, or returns `$00` when empty. |
| `14` | `$FFF4` | `INPUT_CHAR_COUNT` | read-only | Number of bytes currently queued in the text FIFO. |
| `15-19` | `$FFF5-$FFF9` | reserved | - | Reads return zero. Writes are ignored. |

### `INPUT_STATUS`

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `INPUT_TEXT_READY` | The text FIFO contains at least one byte. |
| 1 | `INPUT_KEYBOARD_DOWN` | At least one represented Keyboard/Keypad usage is held. |
| 2 | `INPUT_CONSUMER_DOWN` | At least one represented Consumer usage is held. |
| 3 | `INPUT_MOUSE_DOWN` | At least one mouse button is held. |
| 4 | `INPUT_GAMEPAD_DOWN` | At least one gamepad digital control is held. |
| 5 | `INPUT_SOURCE_CONSOLE` | The console input source owns input. |
| 6 | `INPUT_SOURCE_WIFI` | The Wi-Fi input source owns input. |
| 7 | `INPUT_SOURCE_USB_HOST` | The USB host input source owns input. |

Mouse and gamepad down bits are level summaries. `INPUT_MOUSE_DOWN` is set when
`MOUSE_STATE.buttons & $1F` is nonzero. `INPUT_GAMEPAD_DOWN` is set when any
reported gamepad has an active d-pad bit, digital stick summary bit, or digital
button bit. Analog stick and trigger movement does not affect
`INPUT_GAMEPAD_DOWN`.

Mouse, gamepad, and source capability information is published in
`INPUT_DEVICE_FLAGS` inside the indexed input state block.

## Text FIFO

The text FIFO holds 64 bytes. It stores one byte per character, encoded as the
MIA text character set. The text character set is PETSCII-compatible for normal
Clementina text programs.

When a character arrives and the FIFO is full, MIA drops the oldest byte and
queues the new byte. No overflow flag is kept.

`INPUT_CHAR_COUNT` reports `0-64`. `INPUT_STATUS.INPUT_TEXT_READY` is set when
the count is nonzero. Reading `INPUT_CHAR` pops one byte from the FIFO. Reading
`INPUT_CHAR` while the FIFO is empty returns `$00`.

Keyboard and Wi-Fi key events enqueue text only when they carry a nonzero text
byte. Key releases do not enqueue text. Wi-Fi clients can also send text bytes
directly without changing HID key state.

The console source writes only to the text FIFO. It does not update the HID
bitmap, mouse state, or gamepad state.

## HID Digital Bitmap

MIA exposes two fixed 32-byte HID digital bitmaps:

| Page | HID Usage Page | Meaning |
| ---: | --- | --- |
| `$0007` | Keyboard/Keypad | Standard keyboard keys, modifiers, arrows, function keys, and numeric keypad keys. |
| `$000C` | Consumer | Media and system controls such as mute, volume, play/pause, and similar keys. |

Each bitmap represents usage IDs `$00-$FF` on its HID usage page. Usage ID
`N` is stored at:

```text
byte = N >> 3
bit  = N & 7
mask = 1 << bit
```

A bit is `1` while the matching usage is held and `0` while it is released.
Usage IDs above `$00FF` are not represented by this bitmap.

Examples for the Keyboard/Keypad page:

| Key | HID Usage ID | Bitmap byte | Bit |
| --- | ---: | ---: | ---: |
| `A` | `$04` | `$00` | 4 |
| `TAB` | `$2B` | `$05` | 3 |
| `F1` | `$3A` | `$07` | 2 |
| `F12` | `$45` | `$08` | 5 |
| Right Arrow | `$4F` | `$09` | 7 |
| Left Shift | `$E1` | `$1C` | 1 |
| Left Alt | `$E2` | `$1C` | 2 |
| Right Alt | `$E6` | `$1C` | 6 |

The 6502 reads keyboard and consumer state through the input indexes. Programs
can stream the full bitmap indexes or park one-byte probe indexes on specific
bitmap bytes for repeated polling.

## Input State Memory

MIA publishes live input state and input event controls at fixed MIA RAM offset
`$11000`. State fields are owned by MIA; writes to those fields are not part of
the input interface and may be overwritten by MIA input updates. Event mask and
acknowledge fields are written by the 6502 program through indexed windows.
The block sits outside the video state region and is not synchronized to the
video client.

| Offset | Size | Name | Description |
| ---: | ---: | --- | --- |
| `$11000` | 32 | `KEYBOARD_BITMAP` | HID Keyboard/Keypad page `$0007` bitmap. |
| `$11020` | 32 | `CONSUMER_BITMAP` | HID Consumer page `$000C` bitmap. |
| `$11040` | 5 | `MOUSE_STATE` | Mouse buttons and movement accumulators. |
| `$11045` | 1 | `INPUT_DEVICE_FLAGS` | Active-source capabilities and reported gamepad slots. |
| `$11046` | 1 | `KEYBOARD_EVENT_FLAGS` | Latched keyboard, consumer, and text event causes. |
| `$11047` | 1 | `KEYBOARD_EVENT_MASK` | Event bits that can raise `IRQ_INPUT_KEYBOARD`. |
| `$11048` | 1 | `KEYBOARD_EVENT_ACK` | Write `1` bits to clear `KEYBOARD_EVENT_FLAGS`. |
| `$11049` | 1 | `MOUSE_EVENT_FLAGS` | Latched mouse event causes. |
| `$1104A` | 1 | `MOUSE_EVENT_MASK` | Event bits that can raise `IRQ_INPUT_MOUSE`. |
| `$1104B` | 1 | `MOUSE_EVENT_ACK` | Write `1` bits to clear `MOUSE_EVENT_FLAGS`. |
| `$1104C` | 1 | `GAMEPAD_EVENT_FLAGS` | Latched gamepad event causes. |
| `$1104D` | 1 | `GAMEPAD_EVENT_MASK` | Event bits that can raise `IRQ_INPUT_GAMEPAD`. |
| `$1104E` | 1 | `GAMEPAD_EVENT_ACK` | Write `1` bits to clear `GAMEPAD_EVENT_FLAGS`. |
| `$1104F` | 1 | reserved | Reads as zero. |
| `$11050` | 10 | `GAMEPAD_0` | Gamepad slot 0. |
| `$1105A` | 10 | `GAMEPAD_1` | Gamepad slot 1. |
| `$11064` | 10 | `GAMEPAD_2` | Gamepad slot 2. |
| `$1106E` | 10 | `GAMEPAD_3` | Gamepad slot 3. |
| `$11078` | 8 | reserved | Reads as zero. |

The input block occupies `$11000-$1107F`.

## Input Indexes

MIA preconfigures input indexes for efficient reads through `IDXA_PORT` or
`IDXB_PORT`.

| Index | Name | Address Range | Length | Use |
| ---: | --- | ---: | ---: | --- |
| `$50-$57` | `IIDX_KEYBOARD_PROBE_0-7` | `$11000` default, configurable within `$11000-$1101F` | 1 | eight parked single-byte Keyboard/Keypad bitmap probes |
| `$58-$5F` | `IIDX_CONSUMER_PROBE_0-7` | `$11020` default, configurable within `$11020-$1103F` | 1 | eight parked single-byte Consumer bitmap probes |
| `$60` | `IIDX_INPUT_STATE` | `$11000-$1107F` | 128 | stream the complete input state block |
| `$61` | `IIDX_KEYBOARD_BITMAP` | `$11000-$1101F` | 32 | stream Keyboard/Keypad bitmap |
| `$62` | `IIDX_CONSUMER_BITMAP` | `$11020-$1103F` | 32 | stream Consumer bitmap |
| `$63` | `IIDX_MOUSE_STATE` | `$11040-$11044` | 5 | stream mouse state |
| `$64` | `IIDX_GAMEPAD_0` | `$11050-$11059` | 10 | stream gamepad slot 0 |
| `$65` | `IIDX_GAMEPAD_1` | `$1105A-$11063` | 10 | stream gamepad slot 1 |
| `$66` | `IIDX_GAMEPAD_2` | `$11064-$1106D` | 10 | stream gamepad slot 2 |
| `$67` | `IIDX_GAMEPAD_3` | `$1106E-$11077` | 10 | stream gamepad slot 3 |
| `$68` | `IIDX_INPUT_CONTROL` | `$11045-$1104F` | 11 | stream device flags and event control bytes |

The streaming input indexes use forward step-on-read and wrap over their
configured range. Selecting the index reloads the window data port from the
first byte of the range.

The probe indexes are initialized with read and write stepping disabled, so
repeated reads return the same bitmap byte. Programs position a probe with
`CMD_INPUT_SET_PROBE`. For example, a keyboard probe positioned at byte offset
`$08` reads Keyboard/Keypad usage IDs `$40-$47`.

## Device Flags and Input Events

`INPUT_DEVICE_FLAGS` reports input capabilities and reported gamepad slots for
the active source:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `INPUT_DEVICE_KEYBOARD` | The active source can provide Keyboard/Keypad state. |
| 1 | `INPUT_DEVICE_CONSUMER` | The active source can provide Consumer control state. |
| 2 | `INPUT_DEVICE_MOUSE` | The active source can provide mouse state. |
| 3 | `INPUT_DEVICE_GAMEPAD_0` | Gamepad slot 0 is reported connected. |
| 4 | `INPUT_DEVICE_GAMEPAD_1` | Gamepad slot 1 is reported connected. |
| 5 | `INPUT_DEVICE_GAMEPAD_2` | Gamepad slot 2 is reported connected. |
| 6 | `INPUT_DEVICE_GAMEPAD_3` | Gamepad slot 3 is reported connected. |
| 7 | reserved | Reads as zero. |

In Wi-Fi mode, keyboard, consumer, and mouse availability come from the active
client's capabilities. Gamepad bits come from each reported gamepad slot's
connected flag. USB host mode currently reports no device availability until
USB HID host decoding is added. In console mode, these bits are clear because
console input supplies only text bytes.

Detailed event flags are latched in the input control block. Each event group
has a flags byte, a mask byte, and an acknowledge byte. MIA sets event flag bits
when the corresponding input event occurs. The 6502 enables IRQ generation for
selected event bits by writing the matching mask byte. The 6502 clears handled
event bits by writing `1` bits to the matching acknowledge byte; MIA applies the
acknowledge bits and resets the acknowledge byte to zero.

`KEYBOARD_EVENT_FLAGS` and `KEYBOARD_EVENT_MASK` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `KEY_EVENT_TEXT` | One or more text bytes were queued. |
| 1 | `KEY_EVENT_KEY_DOWN` | One or more Keyboard/Keypad usages changed from up to down. |
| 2 | `KEY_EVENT_KEY_UP` | One or more Keyboard/Keypad usages changed from down to up. |
| 3 | `KEY_EVENT_CONSUMER_DOWN` | One or more Consumer usages changed from up to down. |
| 4 | `KEY_EVENT_CONSUMER_UP` | One or more Consumer usages changed from down to up. |
| 5 | `KEY_EVENT_DEVICE` | Keyboard or Consumer availability changed. |
| 6-7 | reserved | Reads as zero. |

`MOUSE_EVENT_FLAGS` and `MOUSE_EVENT_MASK` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `MOUSE_EVENT_BUTTON_DOWN` | One or more mouse buttons changed from up to down. |
| 1 | `MOUSE_EVENT_BUTTON_UP` | One or more mouse buttons changed from down to up. |
| 2 | `MOUSE_EVENT_MOVE` | `x` or `y` accumulator changed. |
| 3 | `MOUSE_EVENT_SCROLL` | `wheel` or `pan` accumulator changed. |
| 4 | `MOUSE_EVENT_DEVICE` | Mouse availability changed. |
| 5-7 | reserved | Reads as zero. |

`GAMEPAD_EVENT_FLAGS` and `GAMEPAD_EVENT_MASK` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `GAMEPAD_EVENT_BUTTON_DOWN` | One or more gamepad button bits changed from up to down. |
| 1 | `GAMEPAD_EVENT_BUTTON_UP` | One or more gamepad button bits changed from down to up. |
| 2 | `GAMEPAD_EVENT_DPAD` | A gamepad d-pad bit changed. |
| 3 | `GAMEPAD_EVENT_STICK` | A gamepad stick value or digital stick summary changed. |
| 4 | `GAMEPAD_EVENT_TRIGGER` | A gamepad analog trigger changed. |
| 5 | `GAMEPAD_EVENT_DEVICE` | A gamepad slot connected or disconnected. |
| 6-7 | reserved | Reads as zero. |

Input uses three normal MIA IRQ bits:

| IRQ Bit | Name | Set When |
| ---: | --- | --- |
| 8 | `IRQ_INPUT_KEYBOARD` | `KEYBOARD_EVENT_FLAGS & KEYBOARD_EVENT_MASK` is nonzero. |
| 9 | `IRQ_INPUT_MOUSE` | `MOUSE_EVENT_FLAGS & MOUSE_EVENT_MASK` is nonzero. |
| 10 | `IRQ_INPUT_GAMEPAD` | `GAMEPAD_EVENT_FLAGS & GAMEPAD_EVENT_MASK` is nonzero. |

These are ordinary `IRQ_STATUS` bits and are enabled through `IRQ_MASK`.
Reading `$FFF0` clears pending top-level IRQ bits. Detailed input event flags
remain latched until the 6502 writes the matching acknowledge byte.

## Mouse State

`MOUSE_STATE` is a compact five-byte structure:

```c
struct mia_mouse {
    uint8_t buttons;
    uint8_t x;
    uint8_t y;
    uint8_t wheel;
    uint8_t pan;
};
```

Mouse button bits:

| Bit | Button |
| ---: | --- |
| 0 | Left |
| 1 | Right |
| 2 | Middle |
| 3 | Backward |
| 4 | Forward |
| 5-7 | reserved |

`x`, `y`, `wheel`, and `pan` are wrapping movement accumulators. MIA adds each
movement delta to the accumulator modulo 256. Programs compute movement by
subtracting the previous sample from the current sample as signed 8-bit values.

```c
int8_t dx = current.x - previous.x;
int8_t dy = current.y - previous.y;
```

Programs that use mouse movement should poll often enough that the accumulated
delta between polls does not exceed the signed 8-bit range.

## Gamepad State

MIA exposes four gamepad slots. Each slot is a 10-byte structure:

```c
struct mia_gamepad {
    uint8_t dpad;
    uint8_t sticks;
    uint8_t button0;
    uint8_t button1;
    int8_t  lx;
    int8_t  ly;
    int8_t  rx;
    int8_t  ry;
    uint8_t lt;
    uint8_t rt;
};
```

`dpad` bits:

| Bit | Meaning |
| ---: | --- |
| 0 | Direction pad up |
| 1 | Direction pad down |
| 2 | Direction pad left |
| 3 | Direction pad right |
| 4-5 | reserved |
| 6 | Sony-style face labels |
| 7 | Connected |

`sticks` bits:

| Bit | Meaning |
| ---: | --- |
| 0 | Left stick up |
| 1 | Left stick down |
| 2 | Left stick left |
| 3 | Left stick right |
| 4 | Right stick up |
| 5 | Right stick down |
| 6 | Right stick left |
| 7 | Right stick right |

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

Analog fields:

| Field | Range | Meaning |
| --- | --- | --- |
| `lx` | `-128..127` | Left analog stick X, negative left, positive right. |
| `ly` | `-128..127` | Left analog stick Y, negative up, positive down. |
| `rx` | `-128..127` | Right analog stick X, negative left, positive right. |
| `ry` | `-128..127` | Right analog stick Y, negative up, positive down. |
| `lt` | `0..255` | Left analog trigger. |
| `rt` | `0..255` | Right analog trigger. |

Digital stick summary bits are derived from the analog stick values by the
input source. Programs can use the `sticks` byte for simple digital movement or
the signed analog fields for analog control.

## Polling and IRQ Model

Input can be used by polling or by enabling input IRQ bits. MIA does not
require input IRQ handling.

Programs that need text read `INPUT_STATUS` or `INPUT_CHAR_COUNT`, then pop
bytes from `INPUT_CHAR`. Programs that need held-key, mouse, or gamepad state
poll the relevant register or input memory block and compare against a previous
sample when they need press/release edges.

For keyboard and consumer controls, key-down and key-up state is represented by
the bitmap. The order of key transitions is not stored in the bitmap; programs
that need ordering keep their own previous/current samples.

Programs that use input IRQs enable `IRQ_INPUT_KEYBOARD`, `IRQ_INPUT_MOUSE`, or
`IRQ_INPUT_GAMEPAD` in the normal `IRQ_MASK` register, then enable the detailed
event bits they care about in the corresponding input event mask byte. The IRQ
handler reads the detailed event flags, samples the relevant input state, and
writes the handled bits to the matching event acknowledge byte.
