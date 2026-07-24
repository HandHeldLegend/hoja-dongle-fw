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
# Select the hardware layout before importing the Pico SDK.  The HOJA board is
# RP2350-based but has a custom CYW43 pinout; the official Pico W boards use the
# SDK board definitions and intentionally omit the wired Joybus backends.
set(HOJA_DONGLE_BOARD "hoja" CACHE STRING "Target board: hoja, pico_w, or pico2_w")
set_property(CACHE HOJA_DONGLE_BOARD PROPERTY STRINGS hoja pico_w pico2_w)

if(HOJA_DONGLE_BOARD STREQUAL "hoja")
    set(PICO_BOARD pico2_w CACHE STRING "Pico SDK board type" FORCE)
elseif(HOJA_DONGLE_BOARD STREQUAL "pico_w" OR
       HOJA_DONGLE_BOARD STREQUAL "pico2_w")
    set(PICO_BOARD ${HOJA_DONGLE_BOARD} CACHE STRING "Pico SDK board type" FORCE)
    set(HOJA_USB_ONLY ON)
else()
    message(FATAL_ERROR
        "Unknown HOJA_DONGLE_BOARD='${HOJA_DONGLE_BOARD}'. "
        "Expected hoja, pico_w, or pico2_w.")
endif()

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
list(FILTER UTILS_C EXCLUDE REGEX "rgb_stub\\.c$")

if(HOJA_USB_ONLY)
    # The official Pico W boards do not have the HOJA Joybus level/interface
    # hardware or its front-panel LEDs/buttons.
    list(FILTER HAL_C EXCLUDE REGEX "joybus_(n64|gc)_hal\\.c$")
    list(FILTER HAL_C EXCLUDE REGEX "rgb_hal\\.c$")
    list(FILTER UTILS_C EXCLUDE REGEX "rgb\\.c$")
    list(APPEND UTILS_C "${CMAKE_CURRENT_LIST_DIR}/src/utilities/rgb_stub.c")
endif()

add_executable(hoja-dongle-fw 
    src/main.c 
    src/dongle_network.c 
    src/dhcpserver.c 
    ${HAL_C} 
    ${CORES_C} 
    ${TRANSPORTS_C} 
    ${UTILS_C} 
    )

pico_set_program_name(hoja-dongle-fw "hoja-dongle-fw")
pico_set_program_version(hoja-dongle-fw "0.1")

# HOJA production/prototype board pinout.
if(HOJA_DONGLE_BOARD STREQUAL "hoja")
    target_compile_definitions(hoja-dongle-fw PRIVATE
        CYW43_DEFAULT_PIN_WL_REG_ON=21
        CYW43_DEFAULT_PIN_WL_DATA_OUT=22
        CYW43_DEFAULT_PIN_WL_DATA_IN=22
        CYW43_DEFAULT_PIN_WL_HOST_WAKE=22
        CYW43_DEFAULT_PIN_WL_CS=23
        CYW43_DEFAULT_PIN_WL_CLOCK=24
        JOYBUS_DRIVER_DATA_PIN=9
        HOJA_RGB_PIN=17
        HOJA_RGB_COUNT=2
        HOJA_RGB_BRIGHTNESS=48
        HOJA_BTN1_PIN=20
        HOJA_BTN2_PIN=29
        CYW43_USES_VSYS_PIN=0
        )
else()
    target_compile_definitions(hoja-dongle-fw PRIVATE HOJA_USB_ONLY=1)
endif()

# Add the standard library to the build
target_link_libraries(hoja-dongle-fw
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore 
        hardware_watchdog
        tinyusb_device
        tinyusb_board
        )

# Add the standard include files to the build
target_include_directories(hoja-dongle-fw PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/external/HOJA-LIB-DONGLE")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/external/NS-LIB-HID")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/external/SINPUT-LIB-HID")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/external/HHL-TINYUSB-DRIVERS")

target_link_libraries(hoja-dongle-fw
        hoja_lib_dongle
        ns_lib_hid
        sinput_lib_hid
        hhl_tinyusb_drivers
        )

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/nserial.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
if(NOT HOJA_USB_ONLY)
    pico_generate_pio_header(hoja-dongle-fw ${CMAKE_CURRENT_LIST_DIR}/pio/joybus.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
endif()

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(hoja-dongle-fw 0)
pico_enable_stdio_usb(hoja-dongle-fw 0)

pico_add_extra_outputs(hoja-dongle-fw)
