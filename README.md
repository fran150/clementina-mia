# MIA (Multifunction Interface Adapter)

A Raspberry Pi Pico 2 W-based system that provides multiple critical functions for the Clementina 6502 computer:

- **Clock Generation**: Programmable PWM clock (100 kHz boot, 1 MHz normal)
- **ROM Emulation**: Boot-time kernel loading without physical ROM chips
- **Video Output**: Advanced graphics via Wi-Fi with tile-based rendering
- **USB Interface**: Dual-mode USB support (Host/Device) for keyboard input

## Hardware Requirements

- Raspberry Pi Pico 2 W
- Clementina 6502 computer system
- Wi-Fi network for video transmission
- USB devices (keyboard, hub) for Host mode
- Development computer for Device mode

## GPIO Pin Mapping

| GPIO | Function | Description |
|------|----------|-------------|
| 0-9  | Address Bus | A0-A9 (10-bit addressing) |
| 10-17| Data Bus | D0-D7 (bidirectional) |
| 18   | PICOHIRAM | Banks MIA into high memory (active low) |
| 19   | Reset Out | Reset line to Clementina system |
| 20   | WE | Write Enable input from 6502 |
| 21   | OE | Output Enable input from 6502 |
| 22   | ROM CS | ROM Emulation Chip Select |
| 23   | Video CS | Video Chip Select (Device 4) |
| 24   | Gen CS | General Interface Chip Select (Device 0) |
| 25   | Clock Out | PWM clock output to Clementina |
| 26   | USB Mode | Jumper for USB mode selection |

## Memory Mapping

### ROM Emulation ($E000-$FFFF)
- Boot loader code and kernel data streaming
- Reset vector response at $FFFC-$FFFD

### General Interface ($C000-$C3FF)
- $C000-$C0FF: USB keyboard input and status
- $C100: Reset line control

### Video Interface ($D000-$D3FF)
- $D000-$D0FF: Palette bank configuration
- $D100-$D1FF: Character table management
- $D200-$D2FF: OAM data and sprite configuration
- $D300-$D304: PPU control and status registers

## Build Instructions

### Prerequisites

1. Install the Raspberry Pi Pico SDK
2. Set the `PICO_SDK_PATH` environment variable
3. Install CMake (3.13 or later)
4. Install ARM GCC toolchain

### Building

```bash
mkdir build
cd build
cmake ..
make -j4
```

The build will generate `mia.uf2` which can be flashed to the Pico 2 W.

## Development Environment

### TinyUSB Configuration
- Host mode: Multiple device support via USB hub
- Device mode: CDC console for debugging and development

### Wi-Fi Configuration
- Connects to local network for video transmission
- UDP-based communication for low latency
- 30 FPS frame transmission (33.33ms intervals)

## Architecture

### Dual-Core Operation
- **Core 0**: System control (ROM emulation, USB, reset control)
- **Core 1**: Video processing (graphics management, Wi-Fi transmission)

### Boot Sequence
1. Start at 100 kHz clock for ROM emulation
2. Provide boot loader code to 6502
3. Stream kernel data to system memory
4. Transition to 1 MHz for normal operation
5. Enable video processing and Wi-Fi transmission

## Video System

### Graphics Capabilities
- 320x200 pixel resolution
- 8 character tables (256 chars each, 8x8 pixels, 3-bit color)
- 16 palette banks (8 colors each, 16-bit RGB565)
- 256 sprites with configurable size (8x8 or 8x16)
- Double-buffered nametables and palette tables

### Network Transmission
- Frame data: Nametable + Palette table + OAM data
- Resource updates: Character tables and palette banks
- Automatic client discovery and connection

## License

This project is part of the Clementina 6502 computer system.