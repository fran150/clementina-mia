# Kernel Development Guide

## Overview

The MIA firmware supports loading kernel code from an external `kernel.bin` file at compile time. This allows for independent kernel development without modifying the MIA firmware source code.

## Kernel Development Workflow

### 1. Develop Your Kernel

Create your 6502 kernel code using your preferred assembler or compiler. The kernel should:

- Start execution at address `$4000` (where the boot loader will load it)
- Initialize the 6502 system as needed (stack, interrupts, etc.)
- Note: MIA automatically banks out and increases clock to 1 MHz after kernel loading completes

### 2. Generate kernel.bin

Compile/assemble your kernel code to produce a binary file named `kernel.bin`. This file should contain the raw 6502 machine code that will be loaded into memory starting at `$4000`.

### 3. Place kernel.bin

Copy the `kernel.bin` file to the root directory of the MIA project (same directory as `CMakeLists.txt`).

### 4. Build MIA Firmware

Build the MIA firmware using CMake:

```bash
cmake --build build
```

The build system will:
- Automatically detect `kernel.bin`
- Convert it to a C array using `scripts/generate_kernel_data.cmake`
- Embed the kernel data in the MIA firmware
- Generate `mia.uf2` ready for upload

### 5. Upload to Pico

Upload the generated `mia.uf2` file to your Raspberry Pi Pico 2 W.

## Build System Details

### Automatic Kernel Integration

- **Detection**: CMake automatically detects changes to `kernel.bin`
- **Conversion**: The `generate_kernel_data.cmake` script converts the binary to C code
- **Generation**: Creates `build/generated/kernel_data.c` with the kernel as a byte array
- **Embedding**: The kernel data is compiled into the final firmware

### Missing kernel.bin

If `kernel.bin` is not present, the build system will:
- Generate an empty kernel data array
- Display a warning message
- Continue building (MIA will function but with no kernel to load)

### File Locations

- **Source**: `kernel.bin` (project root)
- **Generated**: `build/generated/kernel_data.c` (auto-generated, do not edit)
- **Header**: `src/rom_emulation/kernel_data.h` (external declarations)

## Boot Sequence

1. **MIA Boot Phase**: MIA starts at 100 kHz, provides ROM emulation
2. **Reset Vector**: 6502 reads reset vector, gets boot loader address (`$E000`)
3. **Boot Loader**: Executes MIA-provided boot loader code
4. **Kernel Loading**: Boot loader reads kernel data from MIA via memory-mapped I/O
5. **Automatic Transition**: MIA detects kernel loading completion and automatically:
   - Banks out of high memory space
   - Increases clock from 100 kHz to 1 MHz
6. **Kernel Start**: Boot loader jumps to `$4000` to start your kernel at full speed

## Memory Map

### Boot Loader (provided by MIA)
- `$E000-$E0FF`: Boot loader code space
- `$E100`: Status register (1 = more data, 0 = complete)
- `$E101`: Data register (returns next kernel byte)
- `$FFFC-$FFFD`: Reset vector pointing to `$E000`

### Kernel (your code)
- `$4000+`: Your kernel code loaded here
- Note: MIA automatically banks out after kernel loading, no manual control needed

## Example Kernel Structure

```assembly
; Kernel entry point at $4000
.org $4000

KERNEL_START:
    SEI                     ; Disable interrupts
    CLD                     ; Clear decimal mode
    
    ; Initialize stack
    LDX #$FF
    TXS
    
    ; MIA has already banked out and increased clock to 1 MHz automatically
    ; Your kernel initialization code here
    ; ...
    
    ; Main kernel loop
MAIN_LOOP:
    ; Your main kernel code here
    ; ...
    JMP MAIN_LOOP
```

## Tips

- **Incremental Development**: Only `kernel.bin` needs to change for kernel updates
- **Fast Iteration**: No need to modify or rebuild MIA source code
- **Version Control**: Keep `kernel.bin` in version control for reproducible builds
- **Testing**: Use different `kernel.bin` files to test different kernel versions
- **Debugging**: Check build output for kernel size and loading messages

## Troubleshooting

### Build Fails
- Ensure `kernel.bin` exists in the project root
- Check that CMake can read the file (permissions)
- Verify the file is not corrupted or empty

### Kernel Doesn't Start
- Verify kernel starts at `$4000`
- Check that kernel loading completed successfully (see MIA debug output)
- Ensure kernel doesn't exceed available memory space

### Boot Issues
- Check boot loader messages in MIA debug output
- Verify reset vector handling in your development setup
- Confirm 6502 system is properly connected to MIA