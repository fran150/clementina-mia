# MIA Makefile
# Convenience wrapper for CMake build system

.PHONY: all build clean setup flash help

# Default target
all: build

# Build the project
build:
	@echo "Building MIA firmware..."
	@./build.sh

# Clean build directory
clean:
	@echo "Cleaning build directory..."
	@rm -rf build/

# Setup development environment
setup:
	@echo "Setting up development environment..."
	@./setup_dev.sh

# Flash firmware to Pico (requires picotool)
flash: build
	@echo "Flashing firmware to Pico..."
	@if command -v picotool >/dev/null 2>&1; then \
		picotool load build/mia.uf2 -f; \
	else \
		echo "picotool not found. Please install picotool or manually copy build/mia.uf2 to your Pico."; \
	fi

# Show help
help:
	@echo "MIA (Multifunction Interface Adapter) Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  build   - Build the firmware (default)"
	@echo "  clean   - Clean build directory"
	@echo "  setup   - Setup development environment"
	@echo "  flash   - Flash firmware to Pico (requires picotool)"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Kernel Development:"
	@echo "  Place your compiled kernel.bin in the project root"
	@echo "  Or use: ./update_kernel.sh /path/to/your/kernel.bin"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - Raspberry Pi Pico SDK installed and PICO_SDK_PATH set"
	@echo "  - ARM GCC toolchain (arm-none-eabi-gcc)"
	@echo "  - CMake 3.13 or later"
	@echo "  - kernel.bin file (your compiled 6502 kernel)"
	@echo ""
	@echo "Quick start:"
	@echo "  make setup                              # Setup development environment"
	@echo "  cp /path/to/your/kernel.bin ./kernel.bin  # Copy your kernel"
	@echo "  make build                              # Build firmware with kernel"
	@echo "  make flash                              # Flash to Pico (optional)"