# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(hoja-dongle-fw C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

file(GLOB_RECURSE CORES_C "${CMAKE_CURRENT_LIST_DIR}/src/cores/*.c")
file(GLOB_RECURSE TRANSPORTS_C "${CMAKE_CURRENT_LIST_DIR}/src/transport/*.c")
file(GLOB_RECURSE HAL_C "${CMAKE_CURRENT_LIST_DIR}/src/hal/*.c")
file(GLOB_RECURSE UTILS_C "${CMAKE_CURRENT_LIST_DIR}/src/utilities/*.c")

add_executable(hoja-dongle-fw 
    src/hoja-dongle-fw.c 
    src/dhcpserver.c 
    ${HAL_C} 
    ${CORES_C} 
    ${TRANSPORTS_C} 
    ${UTILS_C} 
    )

pico_set_program_name(hoja-dongle-fw "hoja-dongle-fw")
pico_set_program_version(hoja-dongle-fw "0.1")

# PROTOTYPE BOARD DEFINES
target_compile_definitions(hoja-dongle-fw PRIVATE
        CYW43_DEFAULT_PIN_WL_REG_ON=0
        CYW43_DEFAULT_PIN_WL_DATA_OUT=1
        CYW43_DEFAULT_PIN_WL_DATA_IN=1
        CYW43_DEFAULT_PIN_WL_HOST_WAKE=1
        CYW43_DEFAULT_PIN_WL_CLOCK=2
        CYW43_DEFAULT_PIN_WL_CS=3
        JOYBUS_DRIVER_DATA_PIN=19
        #HOJA_DIAGNOSTICS=1
        )

# Add the standard library to the build
target_link_libraries(hoja-dongle-fw
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore 
        tinyusb_device
        tinyusb_board
        )

# Add the standard include files to the build
target_include_directories(hoja-dongle-fw PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/nserial.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/joybus.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(hoja-dongle-fw 0)
pico_enable_stdio_usb(hoja-dongle-fw 0)

pico_add_extra_outputs(hoja-dongle-fw)
