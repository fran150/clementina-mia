# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Set board type for Pico W (must be set before including SDK)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Set PICO_SDK_PATH if not already set
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} "/Users/fran150/.pico-sdk/sdk/2.2.0")
endif()

# Include the Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set project name and languages
project(mia_multifunction_interface_adapter C CXX ASM)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable
add_executable(mia
    src/main.c
    src/hardware/gpio_mapping.c
    src/system/clock_control.c
    src/system/clock_control_test.c
    src/system/reset_control.c
    src/rom_emulation/rom_emulator.c
    src/rom_emulation/rom_emulator_test.c
    src/video/video_controller.c
    src/usb/usb_controller.c
    src/usb/usb_descriptors.c
    src/network/wifi_controller.c
)

# Add include directories
target_include_directories(mia PRIVATE
    src/
    src/hardware/
    src/system/
    src/rom_emulation/
    src/video/
    src/usb/
    src/network/
)

# Link libraries
target_link_libraries(mia
    pico_stdlib
    pico_multicore
    hardware_gpio
    hardware_pwm
    hardware_clocks
    hardware_pio
    pico_cyw43_arch_lwip_poll
    tinyusb_device
    m  # Math library for floating point operations
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(mia 1)
pico_enable_stdio_uart(mia 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(mia)

# Compiler flags for optimization and debugging
target_compile_options(mia PRIVATE
    -Wall
    -Wextra
    -Werror
    -O2
)

# Define preprocessor macros
target_compile_definitions(mia PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    CYW43_HOST_NAME="MIA-Clementina"
)