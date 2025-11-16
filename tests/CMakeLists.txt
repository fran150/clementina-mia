cmake_minimum_required(VERSION 3.13)

project(mia_tests C)

# Use native compiler (not ARM cross-compiler)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/indexed_memory
)

# Source files for modules under test
# Only include modules that don't require hardware
set(MODULE_SOURCES
    ../src/bus_interface/bus_interface.c
    ../src/irq/irq.c
    ../src/indexed_memory/indexed_memory.c
    mocks/indexed_memory_dma_mock.c
    mocks/bus_sync_pio_mock.c
)

# Test source files
set(TEST_SOURCES
    test_runner.c
    bus_interface/test_bus_interface.c
    bus_interface/test_bus_sync_pio_fifo.c
    indexed_memory/test_indexed_memory.c
    rom_emulation/test_rom_emulator.c
    system/test_clock_control.c
)

# Create test executable
add_executable(mia_tests
    ${MODULE_SOURCES}
    ${TEST_SOURCES}
)

# Link math library if needed
target_link_libraries(mia_tests m)

# Enable testing
enable_testing()
add_test(NAME mia_unit_tests COMMAND mia_tests)
