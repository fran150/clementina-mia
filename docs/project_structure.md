# MIA Project Structure

This document describes the organization of the MIA (Multifunction Interface Adapter) project.

## Directory Structure

```
mia-multifunction-interface-adapter/
├── src/                          # Source code
│   ├── main.c                    # Main entry point and core coordination
│   ├── tusb_config.h            # TinyUSB configuration
│   ├── hardware/                 # Hardware abstraction layer
│   │   ├── gpio_mapping.h/.c     # GPIO pin mapping and bus interface
│   ├── system/                   # System control modules
│   │   ├── clock_control.h/.c    # PWM clock generation
│   │   └── reset_control.h/.c    # Reset line management
│   ├── rom_emulation/           # ROM emulation for boot phase
│   │   └── rom_emulator.h/.c    # Boot loader and kernel streaming
│   ├── video/                   # Video processing system
│   │   └── video_controller.h/.c # Graphics memory and frame processing
│   ├── usb/                     # USB interface management
│   │   └── usb_controller.h/.c  # Dual-mode USB with keyboard buffer
│   └── network/                 # Wi-Fi communication
│       └── wifi_controller.h/.c # Network transmission and frame streaming
├── build/                       # Build output directory (generated)
├── CMakeLists.txt              # CMake build configuration
├── pico_sdk_import.cmake       # Pico SDK integration
├── Makefile                    # Convenience build wrapper
├── build.sh                    # Build script
├── setup_dev.sh               # Development environment setup
├── README.md                   # Project documentation
├── STRUCTURE.md               # This file
└── .gitignore                 # Git ignore rules
```

## Module Responsibilities

### Hardware Layer (`src/hardware/`)
- **gpio_mapping**: Abstracts GPIO pin assignments and bus interface operations
- Provides functions for reading address/data buses and control signals
- Manages bidirectional data bus direction switching

### System Control (`src/system/`)
- **clock_control**: PWM-based clock generation for 6502 system
- **reset_control**: Hardware reset line management with timing control
- Coordinates system-wide state transitions

### ROM Emulation (`src/rom_emulation/`)
- **rom_emulator**: Boot-time ROM functionality using C code at 100 kHz
- Provides boot loader code and kernel data streaming
- Manages PICOHIRAM banking and phase transitions

### Video System (`src/video/`)
- **video_controller**: Graphics memory management and frame processing
- Handles character tables, palette banks, nametables, and sprite data
- Manages memory-mapped I/O for video operations
- Prepares frame data for Wi-Fi transmission

### USB Interface (`src/usb/`)
- **usb_controller**: Dual-mode USB support (Host/Device)
- Keyboard input buffer management with circular buffer
- TinyUSB integration for both modes
- Memory-mapped registers for 6502 access

### Network Communication (`src/network/`)
- **wifi_controller**: Wi-Fi connection and video data transmission
- Frame timing management (30 FPS)
- UDP-based communication with video clients
- Resource update transmission (character tables, palette banks)

## Build System

### CMake Configuration
- **CMakeLists.txt**: Main build configuration for Pico 2 W
- Links required Pico SDK libraries (stdlib, multicore, GPIO, PWM, Wi-Fi, TinyUSB)
- Configures compiler flags and preprocessor definitions

### Build Scripts
- **build.sh**: Main build script with error checking and colored output
- **setup_dev.sh**: Development environment validation and setup
- **Makefile**: Convenience wrapper for common build operations

## Key Design Principles

### Dual-Core Architecture
- **Core 0**: System control (ROM emulation, USB, reset, PIO coordination)
- **Core 1**: Video processing (graphics management, Wi-Fi transmission)
- Inter-core communication for coordinated operation

### Phase-Based Operation
- **Boot Phase**: 100 kHz clock, C-based ROM emulation, kernel loading
- **Normal Phase**: 1 MHz clock, PIO-based video I/O, full functionality

### Memory-Mapped Interface
- **$C000-$C3FF**: Indexed memory interface (multi-window architecture with shared registers)
  - **$C000-$C03F**: Windows A-D (4 windows × 16 registers each)
  - **$C040-$C07F**: Reserved for future windows (E-H)
  - **$C080-$C0FF**: Shared register space (device 
  status, interrupts, identification)
  - **$C100-$C3FF**: Mirrored 3 times (from 6502 perspective)
- **$E000-$FFFF**: ROM emulation space (boot phase only, 256-byte mirrored)

### Hardware Abstraction
- Clean separation between hardware interface and functional modules
- GPIO mapping abstraction for easy pin reassignment
- Standardized control signal handling

## Development Workflow

1. **Setup**: Run `make setup` to validate development environment
2. **Build**: Run `make build` or `./build.sh` to compile firmware
3. **Flash**: Copy `build/mia.uf2` to Pico 2 W or use `make flash`
4. **Debug**: Use USB Device mode for console output and debugging

## Future Expansion

The modular structure supports easy addition of new features:
- Additional video modes in `video_controller`
- Extended USB device support in `usb_controller`
- Network protocol extensions in `wifi_controller`
- Additional I/O devices via new modules in appropriate directories