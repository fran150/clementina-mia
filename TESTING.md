# MIA Testing Guide

This document describes the testing infrastructure for the MIA (Multifunction Interface Adapter) firmware.

## Overview

The MIA project uses a dual testing approach:
1. **Host-based unit tests** - Fast tests that run natively on your development machine
2. **Hardware integration tests** - Optional tests that run on the actual Pico hardware

## Quick Start

### Running Unit Tests

```bash
# Run all unit tests on your local machine (fast!)
make test
```

This will:
- Build tests using your native compiler (gcc/clang)
- Run all hardware-independent tests
- Display results with colored output
- Exit with code 0 if all tests pass, 1 if any fail

### Building Firmware

```bash
# Build firmware for Pico 2 W
make build

# Flash to hardware
make flash
```

## Test Structure

### Directory Layout

```
tests/
├── bus_interface/       # Bus interface module tests
│   ├── test_bus_interface.c
│   └── test_bus_interface.h
├── indexed_memory/      # Indexed memory system tests
│   ├── test_indexed_memory.c
│   └── test_indexed_memory.h
├── rom_emulation/       # ROM emulator tests (hardware-dependent)
│   ├── test_rom_emulator.c
│   └── test_rom_emulator.h
├── system/              # System control tests (hardware-dependent)
│   ├── test_clock_control.c
│   └── test_clock_control.h
├── mocks/               # Mock implementations
│   └── pico_mock.h
├── CMakeLists.txt       # Native test build configuration
├── test_runner.c        # Main test entry point
├── build_and_run.sh     # Build and run script
└── README.md            # Detailed test documentation
```

### Test Categories

#### Hardware-Independent Tests ✅
These tests run natively on your development machine:

- **Bus Interface Tests**
  - Address decoding (8-bit local addresses)
  - Window detection (A-H)
  - Register offset extraction
  - Window state management
  - Multi-window independence

- **Indexed Memory Tests**
  - Memory initialization
  - Index structure validation
  - Basic memory access
  - Auto-stepping functionality
  - Configuration field management
  - DMA operations
  - Window management

#### Hardware-Dependent Tests ⚠️
These tests require actual hardware and are skipped in host mode:

- **ROM Emulator Tests** - Require GPIO, PIO, timing
- **Clock Control Tests** - Require PWM, hardware clocks

## Development Workflow

### Typical Development Cycle

1. **Write code** in `src/`
2. **Run unit tests** with `make test` (< 1 second)
3. **Fix any failures** and repeat
4. **Build firmware** with `make build` when tests pass
5. **Flash to hardware** for integration testing (optional)

### Adding New Tests

1. Create test file in `tests/module_name/`:
   ```c
   // tests/module_name/test_module.c
   #include "test_module.h"
   #include "module_name/module.h"
   #include <stdio.h>
   
   bool test_module_feature(void) {
       printf("Testing feature...\n");
       // Test implementation
       return true;
   }
   
   bool run_module_tests(void) {
       printf("\n=== Running Module Tests ===\n");
       bool all_passed = true;
       all_passed &= test_module_feature();
       return all_passed;
   }
   ```

2. Create header file:
   ```c
   // tests/module_name/test_module.h
   #ifndef TEST_MODULE_H
   #define TEST_MODULE_H
   
   #include <stdbool.h>
   
   bool run_module_tests(void);
   
   #endif
   ```

3. Add to `tests/test_runner.c`:
   ```c
   #include "module_name/test_module.h"
   
   // In main():
   if (run_module_tests()) {
       passed_suites++;
   }
   ```

4. Add to `tests/CMakeLists.txt`:
   ```cmake
   set(MODULE_SOURCES
       ../src/module_name/module.c
       # ... other sources
   )
   
   set(TEST_SOURCES
       module_name/test_module.c
       # ... other tests
   )
   ```

## Benefits of This Approach

### Fast Iteration
- Unit tests run in < 1 second
- No need to flash hardware for every change
- Immediate feedback during development

### CI/CD Ready
- Tests can run in continuous integration
- No hardware required for automated testing
- Easy to integrate with GitHub Actions, etc.

### Better Debugging
- Use native debuggers (gdb, lldb)
- Use sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer)
- Use profilers and analysis tools

### Clean Separation
- Production code in `src/`
- Test code in `tests/`
- No test code mixed with firmware

## Technical Details

### Mock Implementation

The `tests/mocks/pico_mock.h` file provides minimal mocks for Pico SDK functions:
- `printf` - Uses native printf
- `tight_loop_contents` - No-op
- `sleep_ms/sleep_us` - No-op (or can use actual sleep if needed)

### Build System

- **Firmware build**: Uses ARM cross-compiler (arm-none-eabi-gcc) via Pico SDK
- **Test build**: Uses native compiler (gcc/clang) with standard C library
- Both builds share the same source files from `src/`

### Test Output

Tests provide colored output for easy reading:
- 🟢 **Green**: Tests passed
- 🔴 **Red**: Tests failed  
- 🟡 **Yellow**: Build/run status messages

## Troubleshooting

### Tests won't build
```bash
# Clean test build
rm -rf tests/build
make test
```

### Firmware won't build
```bash
# Clean firmware build
make clean
make build
```

### Need to test hardware-specific code
Hardware-dependent tests are skipped in host mode. To test them:
1. Build firmware with tests enabled (modify CMakeLists.txt)
2. Flash to Pico hardware
3. Monitor serial output for test results

## Future Enhancements

Potential improvements to the testing infrastructure:

- [ ] Add code coverage reporting
- [ ] Integrate with CI/CD (GitHub Actions)
- [ ] Add performance benchmarks
- [ ] Add memory leak detection (Valgrind)
- [ ] Add more comprehensive mocks for hardware simulation
- [ ] Consider Renode for full system emulation

## Questions?

See `tests/README.md` for more detailed information about the test infrastructure.
