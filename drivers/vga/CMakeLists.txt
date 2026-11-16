add_library(vga INTERFACE)

target_sources(vga INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/vga.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
        ${CMAKE_CURRENT_LIST_DIR}/vga_crtc.c
)

set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/vga_crtc.c
        DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "-O3;-fno-tree-loop-distribute-patterns")

target_compile_definitions(vga INTERFACE VGA=1)

# The same pins and the same PIO program, at the 15 kHz 240p a SCART set takes.
if (RGBS)
    target_compile_definitions(vga INTERFACE VGA_RGBS=1)
endif()

target_link_libraries(vga INTERFACE
        graphics
        hardware_pio
        hardware_dma
        hardware_gpio
        hardware_irq
        hardware_clocks
)

target_include_directories(vga INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
