add_library(hstx-vga-hdmi-audio INTERFACE)

target_sources(hstx-vga-hdmi-audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/hstx_video.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
        ${CMAKE_CURRENT_LIST_DIR}/hstx_audio.c
        ${CMAKE_CURRENT_LIST_DIR}/hstx_data_island.c
)
target_compile_definitions(hstx-vga-hdmi-audio INTERFACE PICO_DIVIDER_IN_RAM=1)
target_link_libraries(hstx-vga-hdmi-audio INTERFACE
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_gpio
        hardware_irq
        hardware_pll
        hardware_resets
        hardware_sync
)
target_include_directories(hstx-vga-hdmi-audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../graphics
)

# Do not turn either scanline renderer's stores into flash-resident newlib
# calls. The INTERFACE sources are compiled by the root executable, so set
# their properties in that directory (not just this subdirectory's scope).
set_source_files_properties(
        ${CMAKE_CURRENT_LIST_DIR}/hstx_video.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
        ${CMAKE_CURRENT_LIST_DIR}/hstx_audio.c
        DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "-fno-tree-loop-distribute-patterns"
)
