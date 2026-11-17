add_library(graphics INTERFACE)

target_sources(graphics INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics_modes.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics_text.c
)

# Scanline loops must stay fast and must not turn into flash-resident memset.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/graphics_modes.c
        DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "-O3;-fno-tree-loop-distribute-patterns")

target_link_libraries(graphics INTERFACE
        hardware_clocks
        hardware_vreg
        pico_time
)

target_include_directories(graphics INTERFACE ${CMAKE_CURRENT_LIST_DIR})
