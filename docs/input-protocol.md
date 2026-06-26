# MIA Wi-Fi Input Protocol

This document defines the UDP protocol used by a Wi-Fi input client to supply
keyboard, consumer, text, mouse, and gamepad input to MIA. The 6502-facing
input interface is defined in [input.md](input.md), and usage examples are in
[input-programmer-guide.md](input-programmer-guide.md).

The input protocol uses UDP port `6503` by default. Video uses UDP port `6502`.
Input packets are small and independent of video update packets.

Wi-Fi input is enabled by issuing the 6502-facing `CMD_INPUT_SET_MODE` command
with `INPUT_MODE_WIFI`, by the terminal console command `input wifi`, or, in a
USB-device build, at boot by default (`MIA_INPUT_DEFAULT_MODE=wifi`).

## Transport

- Transport: UDP.
- Default port: `6503`.
- Byte order: little-endian.
- Version: `1`.
- Client model: one accepted client owns Wi-Fi input until it disconnects, a
  replacement `HELLO` is accepted, or the input mode changes.
- Reliability: best effort. State packets repair dropped event packets.

MIA accepts a compatible `HELLO` when Wi-Fi input mode is active. If another
Wi-Fi client already owns the session, the accepted `HELLO` replaces that
session, clears live keyboard, consumer, mouse, and gamepad state, and assigns a
new session token. Queued text bytes remain in the text FIFO.

When Wi-Fi input mode is not active, MIA replies to `HELLO` with `WELCOME`
status `BUSY`.

## Packet Header

Every packet starts with a 12-byte header:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | ASCII `MIIN`. |
| 4 | 1 | `version` | Protocol version, `1`. |
| 5 | 1 | `type` | Packet type. |
| 6 | 2 | `seq` | Client sequence number. |
| 8 | 4 | `session` | Session token. `0` only for `HELLO`. |

The client increments `seq` for each packet it sends in a session. MIA ignores
duplicate or older sequence numbers from the active session. Sequence comparison
uses unsigned 16-bit wraparound.

`HELLO` uses session `0`. `WELCOME` returns the accepted session token. All
later client packets use that token.

For `WELCOME`, the header `session` field is zero and the payload `session`
field carries the assigned token.

## Packet Types

| Type | Name | Direction | Payload |
| ---: | --- | --- | --- |
| `$01` | `HELLO` | client to MIA | capabilities and optional client name |
| `$02` | `WELCOME` | MIA to client | status, session, capabilities |
| `$04` | `DISCONNECT` | client to MIA | none |
| `$10` | `TEXT` | client to MIA | PETSCII-compatible text bytes |
| `$11` | `HID_EVENT` | client to MIA | one keyboard or consumer press/release |
| `$12` | `HID_BITMAP` | client to MIA | complete keyboard or consumer bitmap |
| `$20` | `MOUSE_DELTA` | client to MIA | mouse buttons and movement deltas |
| `$30` | `GAMEPAD_STATE` | client to MIA | one complete gamepad slot |
| `$31` | `GAMEPAD_CLEAR` | client to MIA | clear one gamepad slot |
| `$40` | `CLEAR_STATE` | client to MIA | clear selected input state |

Unknown packet types are ignored.

## Capabilities

Capability fields are 16-bit bitsets:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `CAP_TEXT` | Client can send `TEXT` and key-event text bytes. |
| 1 | `CAP_KEYBOARD` | Client can send Keyboard/Keypad page state. |
| 2 | `CAP_CONSUMER` | Client can send Consumer page state. |
| 3 | `CAP_MOUSE` | Client can send mouse state. |
| 4 | `CAP_GAMEPAD` | Client can send gamepad state. |
| 5-15 | reserved | Send zero. |

## `HELLO` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | `capabilities` |
| 2 | 1 | `name_len` |
| 3 | `name_len` | UTF-8 client name bytes |

The client name is diagnostic only. MIA accepts packets based on source address,
source UDP port, version, and session token.

When a client is accepted, MIA uses the accepted capability bits to publish
keyboard, consumer, and mouse availability in `INPUT_DEVICE_FLAGS`.

## `WELCOME` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | `status` |
| 1 | 4 | `session` |
| 5 | 2 | `capabilities` |

`status` values:

| Value | Name | Meaning |
| ---: | --- | --- |
| `$00` | `ACCEPTED` | Client owns Wi-Fi input. |
| `$01` | `BUSY` | Wi-Fi input mode is not active. |
| `$02` | `UNSUPPORTED_VERSION` | MIA does not support the requested version. |

`session` is nonzero when status is `ACCEPTED`. For non-accepted responses,
`session` is zero.

## `DISCONNECT`

`DISCONNECT` has no payload. It clears live keyboard, consumer, mouse, and
gamepad state for the Wi-Fi source, releases the Wi-Fi input session, and leaves
queued text bytes in the text FIFO.

## `TEXT` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | `count` |
| 1 | `count` | text bytes |

The payload bytes are MIA text bytes. The text character set is
PETSCII-compatible for Clementina text programs. `TEXT` can carry any byte value
including `$00`.

MIA appends the bytes to the 64-byte text FIFO. If the FIFO is full, MIA drops
oldest bytes as needed.

`TEXT` does not change keyboard, consumer, mouse, or gamepad held-state.
Appending one or more bytes sets `KEY_EVENT_TEXT`.

## `HID_EVENT` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | `usage_page` |
| 2 | 2 | `usage_id` |
| 4 | 1 | `flags` |
| 5 | 1 | `text` |

Supported `usage_page` values:

| Page | Meaning |
| ---: | --- |
| `$0007` | HID Keyboard/Keypad |
| `$000C` | HID Consumer |

`flags` bits:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `DOWN` | `1` means pressed; `0` means released. |
| 1 | `REPEAT` | Key repeat event. `DOWN` is also set. |
| 2-7 | reserved | Send zero. |

If `usage_id <= $00FF`, MIA updates the matching bit in the internal bitmap for
the selected usage page. Keyboard and consumer usages above `$00FF` are not
represented by the 32-byte bitmap and are ignored for bitmap state.

For Keyboard/Keypad events, if `DOWN` is set and `text` is nonzero, MIA appends
`text` to the text FIFO. Release events do not enqueue text. To enqueue byte
`$00`, use `TEXT`.

Consumer events do not enqueue text.

Keyboard/Keypad transitions set `KEY_EVENT_KEY_DOWN` or `KEY_EVENT_KEY_UP`.
Consumer transitions set `KEY_EVENT_CONSUMER_DOWN` or
`KEY_EVENT_CONSUMER_UP`. A Keyboard/Keypad event that queues text also sets
`KEY_EVENT_TEXT`.

## `HID_BITMAP` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | `usage_page` |
| 2 | 32 | `bitmap` |

Supported pages are `$0007` Keyboard/Keypad and `$000C` Consumer.

`HID_BITMAP` replaces the complete internal bitmap for the given page. It does
not enqueue text. Clients send a full bitmap after connect, after focus regain,
and while controls are held so that dropped `HID_EVENT` packets cannot leave
stale held-state indefinitely.

MIA compares the new bitmap with the previous bitmap and sets the matching
down/up event flags for changed bits.

## `MOUSE_DELTA` Payload

| Offset | Size | Field | Type |
| ---: | ---: | --- | --- |
| 0 | 1 | `buttons` | bitfield |
| 1 | 1 | `dx` | signed 8-bit |
| 2 | 1 | `dy` | signed 8-bit |
| 3 | 1 | `wheel` | signed 8-bit |
| 4 | 1 | `pan` | signed 8-bit |

Mouse button bits:

| Bit | Button |
| ---: | --- |
| 0 | Left |
| 1 | Right |
| 2 | Middle |
| 3 | Backward |
| 4 | Forward |
| 5-7 | reserved |

MIA writes `buttons` to `MOUSE_STATE.buttons` and adds the signed deltas to the
wrapping mouse accumulators:

```text
MOUSE_STATE.x     += dx
MOUSE_STATE.y     += dy
MOUSE_STATE.wheel += wheel
MOUSE_STATE.pan   += pan
```

All accumulator additions wrap modulo 256. If the client has a larger movement
delta than fits in signed 8-bit, it sends multiple `MOUSE_DELTA` packets.

Button transitions set `MOUSE_EVENT_BUTTON_DOWN` or
`MOUSE_EVENT_BUTTON_UP`. Nonzero `dx` or `dy` sets `MOUSE_EVENT_MOVE`.
Nonzero `wheel` or `pan` sets `MOUSE_EVENT_SCROLL`.

## `GAMEPAD_STATE` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | `player` |
| 1 | 10 | `state` |

`player` is `0-3`. MIA copies `state` into the matching gamepad slot. Values
outside `0-3` are ignored.

The 10-byte state layout is:

| Offset | Name | Type |
| ---: | --- | --- |
| 0 | `dpad` | bitfield |
| 1 | `sticks` | bitfield |
| 2 | `button0` | bitfield |
| 3 | `button1` | bitfield |
| 4 | `lx` | signed 8-bit |
| 5 | `ly` | signed 8-bit |
| 6 | `rx` | signed 8-bit |
| 7 | `ry` | signed 8-bit |
| 8 | `lt` | unsigned 8-bit |
| 9 | `rt` | unsigned 8-bit |

`dpad` bit 7 is the connected flag. Sending a state with `dpad` bit 7 clear
leaves the slot disconnected.

MIA updates the matching gamepad bit in `INPUT_DEVICE_FLAGS` from the connected
flag. Button transitions set `GAMEPAD_EVENT_BUTTON_DOWN` or
`GAMEPAD_EVENT_BUTTON_UP`. D-pad, stick, trigger, and connected-state changes
set their matching `GAMEPAD_EVENT_*` flags.

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

Analog values use these ranges:

| Field | Range |
| --- | --- |
| `lx`, `ly`, `rx`, `ry` | signed `-128..127` |
| `lt`, `rt` | unsigned `0..255` |

## `GAMEPAD_CLEAR` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | `player` |

`player` is `0-3`. MIA clears the selected gamepad slot to zero. Values outside
`0-3` are ignored.

If the selected slot was connected or had nonzero state, MIA updates
`INPUT_DEVICE_FLAGS` and sets the matching gamepad event flags.

## `CLEAR_STATE` Payload

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | `mask` |

`mask` bits:

| Bit | Meaning |
| ---: | --- |
| 0 | Clear text FIFO |
| 1 | Clear Keyboard/Keypad bitmap |
| 2 | Clear Consumer bitmap |
| 3 | Clear mouse state |
| 4 | Clear all gamepad slots |
| 5-6 | reserved |
| 7 | Clear all input state named above |

`CLEAR_STATE` affects only the active Wi-Fi session.

When `CLEAR_STATE` changes held keyboard, consumer, mouse, or gamepad state, MIA
sets the matching release, movement, button, and device event flags.

## Client Behavior

A Wi-Fi client sends:

1. `HELLO`.
2. `HID_BITMAP` for Keyboard/Keypad and Consumer pages it supports.
3. `HID_EVENT` for key and consumer transitions.
4. `TEXT` for paste, IME, or text that is not tied to a physical key.
5. `MOUSE_DELTA` for mouse movement and button snapshots.
6. `GAMEPAD_STATE` whenever a gamepad slot changes.
7. `DISCONNECT` when the client exits cleanly.

On focus loss, the client sends `CLEAR_STATE` for keyboard, consumer, mouse, and
gamepads. On focus regain, it sends full `HID_BITMAP` and `GAMEPAD_STATE`
snapshots before sending new events.

Console input does not use this protocol and does not supply mouse or gamepad
state.
