add_library(st7789 INTERFACE)

target_sources(st7789 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/st7789.c)
pico_generate_pio_header(st7789 ${CMAKE_CURRENT_LIST_DIR}/st7789.pio)

target_compile_definitions(st7789 INTERFACE TFT=1)

target_link_libraries(st7789 INTERFACE
        graphics
        hardware_gpio
        hardware_pio
        hardware_sync
        hardware_dma
        pico_stdlib
)

target_include_directories(st7789 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
