cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(rx2joy C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(rx2joy
    src/main.c
    src/channels.c
    src/inputs.c
    src/calstore.c
    src/display.c
    src/ssd1306.c
    src/shared_state.c
    src/usb_descriptors.c
)

pico_generate_pio_header(rx2joy ${CMAKE_CURRENT_LIST_DIR}/src/pwm_capture.pio)

target_include_directories(rx2joy PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(rx2joy
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_uart
    hardware_irq
    hardware_i2c
    hardware_adc
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
)

# La consola va por el CDC propio definido en usb_descriptors.c.
# Activar pico_stdio_usb ademas montaria un segundo dispositivo CDC y
# los dos pelearian por los mismos endpoints.
pico_enable_stdio_usb(rx2joy 0)
pico_enable_stdio_uart(rx2joy 0)

pico_add_extra_outputs(rx2joy)
