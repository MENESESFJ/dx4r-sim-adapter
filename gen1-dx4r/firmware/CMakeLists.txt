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

# ---------------------------------------------------------------------
# peak_probe: herramienta de un solo uso para medir el HIGH real del
# SR2000 antes de soldar la version definitiva. No forma parte del
# firmware final -- se flashea aparte, se usa, y se vuelve a main.c.
# Por eso usa pico_enable_stdio_usb en vez de los descriptores propios
# de dx4r_sim_adapter: es mas simple y no necesita ser HID.
# ---------------------------------------------------------------------
add_executable(peak_probe
    tools/peak_probe/peak_probe.c
)

target_link_libraries(peak_probe
    pico_stdlib
    hardware_adc
)

pico_enable_stdio_usb(peak_probe 1)
pico_enable_stdio_uart(peak_probe 0)

pico_add_extra_outputs(peak_probe)
