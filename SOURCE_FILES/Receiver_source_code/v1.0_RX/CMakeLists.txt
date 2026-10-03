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
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK. The bundled pico_sdk_import.cmake finds it from the
# PICO_SDK_PATH environment variable, a -DPICO_SDK_PATH=... option, or
# downloads it when -DPICO_SDK_FETCH_FROM_GIT=ON is given.
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

project(duchess_receiver C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

add_executable(duchess_receiver
    src/main.c
    src/ble_hid_client.c
    src/xid_device.c
    src/gamepad_state.c
)

# We need BTStack for BLE central role and TinyUSB for USB device
target_include_directories(duchess_receiver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(duchess_receiver
    pico_stdlib
    pico_cyw43_arch_threadsafe_background
    pico_btstack_ble
    pico_btstack_cyw43
    tinyusb_device
    tinyusb_board
    hardware_flash
)

# Use the CYW43 wireless chip for Bluetooth
target_compile_definitions(duchess_receiver PRIVATE
    PICO_STDIO_USB=0
    PICO_STDIO_UART=1
    CYW43_LWIP=0
    RUNNING_AS_CLIENT=1
)

# Generate UF2 output for drag-and-drop flashing
pico_add_extra_outputs(duchess_receiver)

# Enable UART output on default pins (GP0=TX, GP1=RX) for debug
pico_enable_stdio_uart(duchess_receiver 1)
pico_enable_stdio_usb(duchess_receiver 0)
