cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(dx4r_sim_adapter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(dx4r_sim_adapter
    main.c
    usb_descriptors.c
    shared_state.c
    display.c
    ssd1306.c
)

pico_generate_pio_header(dx4r_sim_adapter
    ${CMAKE_CURRENT_LIST_DIR}/pwm_capture.pio
)

target_include_directories(dx4r_sim_adapter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(dx4r_sim_adapter
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_i2c
    hardware_adc
    tinyusb_device
    tinyusb_board
)

# Nada de pico_enable_stdio_usb: los descriptores USB son nuestros.
pico_add_extra_outputs(dx4r_sim_adapter)
