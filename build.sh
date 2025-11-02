#!/bin/bash

# MIA Build Script
# Builds the MIA firmware for Raspberry Pi Pico 2 W

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Handle clean option
if [ "$1" = "clean" ]; then
    echo -e "${YELLOW}Cleaning MIA build directory${NC}"
    rm -rf build/
    echo -e "${GREEN}Build directory cleaned${NC}"
    exit 0
fi

echo -e "${YELLOW}Building MIA (Multifunction Interface Adapter)${NC}"

# Check if PICO_SDK_PATH is set
if [ -z "$PICO_SDK_PATH" ]; then
    echo -e "${RED}Error: PICO_SDK_PATH environment variable is not set${NC}"
    echo "Please set PICO_SDK_PATH to the location of your Pico SDK installation"
    exit 1
fi

echo -e "Using Pico SDK at: ${GREEN}$PICO_SDK_PATH${NC}"

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

# Change to build directory
cd build

# Run CMake configuration
echo "Configuring build with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "Building project..."
make -j$(nproc)

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo -e "Generated files:"
    echo -e "  ${GREEN}mia.uf2${NC} - Flash this file to your Pico 2 W"
    echo -e "  ${GREEN}mia.elf${NC} - ELF binary for debugging"
    echo -e "  ${GREEN}mia.bin${NC} - Raw binary"
    echo -e "  ${GREEN}mia.hex${NC} - Intel HEX format"
    
    # Show file sizes
    echo -e "\nFile sizes:"
    ls -lh mia.uf2 mia.elf mia.bin 2>/dev/null || true
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi