#!/bin/bash

# MIA Development Environment Setup Script
# Sets up the development environment for MIA firmware

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${YELLOW}MIA Development Environment Setup${NC}"
echo "This script will help you set up the development environment for MIA firmware."
echo

# Check operating system
OS=$(uname -s)
echo -e "Detected OS: ${GREEN}$OS${NC}"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for required tools
echo -e "\n${BLUE}Checking required tools...${NC}"

# Check CMake
if command_exists cmake; then
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    echo -e "✓ CMake found: ${GREEN}$CMAKE_VERSION${NC}"
else
    echo -e "✗ ${RED}CMake not found${NC}"
    echo "Please install CMake 3.13 or later"
    exit 1
fi

# Check ARM GCC
if command_exists arm-none-eabi-gcc; then
    GCC_VERSION=$(arm-none-eabi-gcc --version | head -n1 | cut -d' ' -f9)
    echo -e "✓ ARM GCC found: ${GREEN}$GCC_VERSION${NC}"
else
    echo -e "✗ ${RED}ARM GCC toolchain not found${NC}"
    echo "Please install arm-none-eabi-gcc toolchain"
    case $OS in
        "Darwin")
            echo "  brew install --cask gcc-arm-embedded"
            ;;
        "Linux")
            echo "  sudo apt-get install gcc-arm-none-eabi"
            ;;
    esac
    exit 1
fi

# Check Git
if command_exists git; then
    echo -e "✓ Git found"
else
    echo -e "✗ ${RED}Git not found${NC}"
    echo "Please install Git"
    exit 1
fi

# Check for Pico SDK
echo -e "\n${BLUE}Checking Pico SDK...${NC}"

if [ -n "$PICO_SDK_PATH" ]; then
    if [ -d "$PICO_SDK_PATH" ]; then
        echo -e "✓ PICO_SDK_PATH set: ${GREEN}$PICO_SDK_PATH${NC}"
        
        # Check if it's a valid SDK
        if [ -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
            echo -e "✓ Valid Pico SDK found"
        else
            echo -e "✗ ${RED}Invalid Pico SDK path${NC}"
            echo "The directory exists but doesn't contain pico_sdk_init.cmake"
            exit 1
        fi
    else
        echo -e "✗ ${RED}PICO_SDK_PATH directory does not exist${NC}"
        exit 1
    fi
else
    echo -e "✗ ${RED}PICO_SDK_PATH not set${NC}"
    echo "Please set the PICO_SDK_PATH environment variable"
    echo
    echo "To download and set up the Pico SDK:"
    echo "  git clone https://github.com/raspberrypi/pico-sdk.git"
    echo "  cd pico-sdk"
    echo "  git submodule update --init"
    echo "  export PICO_SDK_PATH=\$(pwd)"
    echo
    echo "Add the export line to your shell profile (.bashrc, .zshrc, etc.)"
    exit 1
fi

# Check TinyUSB submodule
TINYUSB_PATH="$PICO_SDK_PATH/lib/tinyusb"
if [ -d "$TINYUSB_PATH" ] && [ -f "$TINYUSB_PATH/src/tusb.h" ]; then
    echo -e "✓ TinyUSB found in Pico SDK"
else
    echo -e "⚠ ${YELLOW}TinyUSB not found or incomplete${NC}"
    echo "Running git submodule update in Pico SDK..."
    cd "$PICO_SDK_PATH"
    git submodule update --init --recursive
    cd - > /dev/null
fi

# All checks passed
echo -e "\n${GREEN}✓ Development environment setup complete!${NC}"
echo
echo -e "${BLUE}Next steps:${NC}"
echo "1. Run ./build.sh to build the firmware"
echo "2. Flash the generated mia.uf2 file to your Pico 2 W"
echo "3. Connect the MIA to your Clementina system"
echo
echo -e "${BLUE}Useful commands:${NC}"
echo "  ./build.sh          - Build the firmware"
echo "  ./build.sh clean    - Clean build directory"
echo "  make -C build       - Build from build directory"
echo
echo -e "${YELLOW}Happy coding!${NC}"