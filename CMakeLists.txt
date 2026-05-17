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
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Set PICO_SDK_PATH if not already set
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} "/Users/fran150/.pico-sdk/sdk/2.2.0")
endif()

# Include the Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set project name and languages
project(mia C CXX ASM)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Generate kernel data from kernel.bin at build time
set(KERNEL_BIN_FILE "${CMAKE_SOURCE_DIR}/kernel.bin")
set(KERNEL_DATA_FILE "${CMAKE_BINARY_DIR}/generated/kernel_data.c")

# Create the generated directory
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

# Custom command to generate kernel_data.c from kernel.bin
add_custom_command(
    OUTPUT ${KERNEL_DATA_FILE}
    COMMAND ${CMAKE_COMMAND} 
        -DKERNEL_BIN_FILE=${KERNEL_BIN_FILE}
        -DKERNEL_DATA_FILE=${KERNEL_DATA_FILE}
        -P ${CMAKE_SOURCE_DIR}/scripts/generate_kernel_data.cmake
    DEPENDS ${KERNEL_BIN_FILE} ${CMAKE_SOURCE_DIR}/scripts/generate_kernel_data.cmake
    COMMENT "Generating kernel data from kernel.bin"
    VERBATIM
)

# Add executable
add_executable(mia
    src/mia/main.c
    src/mia/etc/err.c
    src/mia/cmds/cmds.c
    src/mia/mem/dma.c
    src/mia/mem/indexes.c
    src/mia/mem/mem.c
    src/mia/mem/regs.c
    src/mia/sys/mia.c
    src/mia/sys/reset.c
    src/mia/sys/speed.c
    ${KERNEL_DATA_FILE}  # Add generated kernel data
)

# Copy and execute program in RAM
pico_set_binary_type(mia copy_to_ram)

# Generate PIO headers
pico_generate_pio_header(mia ${CMAKE_SOURCE_DIR}/src/mia/sys/sys.pio)

# Add include directories
target_include_directories(mia PRIVATE
    src/
    src/mia
    src/mia/etc
    src/mia/cmds
    src/mia/hardware
    src/mia/irq
    src/mia/mem
    src/mia/rom
    src/mia/sys
    ${CMAKE_BINARY_DIR}/generated  # Include generated files directory
)

# Link libraries
target_link_libraries(mia
    pico_stdlib
    pico_multicore
    pico_bootrom
    hardware_gpio
    hardware_pwm
    hardware_clocks
    hardware_pio
    hardware_dma
    pico_cyw43_arch_lwip_poll
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
)

# Define preprocessor macros
target_compile_definitions(mia PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    CYW43_HOST_NAME="MIA-Clementina"
)
