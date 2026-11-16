add_library(graphics INTERFACE)

target_sources(graphics INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics_modes.c
)

# Scanline loops must stay fast and must not turn into flash-resident memset.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/graphics_modes.c
        DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "-O3;-fno-tree-loop-distribute-patterns")

if (HDMI OR HDMI_HSTX)
    target_sources(graphics INTERFACE ${CMAKE_CURRENT_LIST_DIR}/graphics_text.c)
endif()

target_link_libraries(graphics INTERFACE
        hardware_clocks
        hardware_vreg
        pico_time
)

# OFF drops the static 320 x 240 buffer, so an application which supplies its
# own picture buffer does not pay 76,800 bytes of RAM for an unused one.
option(GRAPHICS_BUILTIN_FRAMEBUFFER "Provide a built-in 320x240 framebuffer" ON)
if (NOT GRAPHICS_BUILTIN_FRAMEBUFFER)
    target_compile_definitions(graphics INTERFACE GRAPHICS_NO_BUILTIN_FRAMEBUFFER=1)
endif()

# ON stops graphics_init() touching the system clock and core voltage. Needed
# when the application must fix the clock before it starts a second core.
option(GRAPHICS_NO_CLOCK_SETUP "Application owns the system clock" OFF)
if (GRAPHICS_NO_CLOCK_SETUP)
    target_compile_definitions(graphics INTERFACE GRAPHICS_NO_CLOCK_SETUP=1)
endif()

target_include_directories(graphics INTERFACE ${CMAKE_CURRENT_LIST_DIR})
