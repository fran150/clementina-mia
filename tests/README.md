# MIA Unit Tests

This directory contains unit tests for the MIA (Multifunction Interface Adapter) firmware that can run natively on your development machine without requiring hardware.

## Running Tests

```bash
# From project root
make test

# Or directly from tests directory
cd tests
./build_and_run.sh
```

## Test Structure

```
tests/
├── bus_interface/       # Bus interface tests
├── indexed_memory/      # Indexed memory system tests
├── rom_emulation/       # ROM emulator tests (hardware-dependent, skipped)
├── system/              # System tests (hardware-dependent, skipped)
├── mocks/               # Mock implementations of Pico SDK
├── CMakeLists.txt       # Native build configuration
├── test_runner.c        # Main test entry point
└── build_and_run.sh     # Build and run script
```

## Test Categories

### Hardware-Independent Tests (Run on Host)
- **Bus Interface Tests**: Address decoding, window management, state tracking
- **Indexed Memory Tests**: Memory operations, auto-stepping, configuration

These tests run natively and provide fast feedback during development.

### Hardware-Dependent Tests (Skipped on Host)
- **ROM Emulator Tests**: Require GPIO, PIO, and timing hardware
- **Clock Control Tests**: Require PWM and hardware clocks

These tests are skipped when running on the host machine. They can be enabled in the firmware build for on-device testing.

## Adding New Tests

1. Create test file in appropriate subdirectory: `tests/module_name/test_module.c`
2. Create header file: `tests/module_name/test_module.h`
3. Implement test functions and a `run_module_tests()` function
4. Add to `test_runner.c`
5. Add source files to `tests/CMakeLists.txt`

## Test Output

Tests provide colored output:
- ✓ Green: Tests passed
- ✗ Red: Tests failed
- Yellow: Build/run status messages

## Requirements

- CMake 3.13 or later
- GCC or Clang (native compiler)
- Standard C library

No Pico SDK or ARM toolchain required for running tests!
