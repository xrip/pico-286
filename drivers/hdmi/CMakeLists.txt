add_library(hdmi INTERFACE)

target_sources(hdmi INTERFACE ${CMAKE_CURRENT_LIST_DIR}/hdmi.c)
target_sources(hdmi INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../graphics/graphics_pc_hdmi.c)
pico_generate_pio_header(hdmi ${CMAKE_CURRENT_LIST_DIR}/hdmi_text.pio)

# Optional oscilloscope/logic-analyser marker, disabled without code or RAM cost.
set(HDMI_PROFILE_GPIO "-1" CACHE STRING "GPIO high while the HDMI DMA IRQ runs; -1 disables")
target_compile_definitions(hdmi INTERFACE HDMI_PROFILE_GPIO=${HDMI_PROFILE_GPIO})

# RP2040's scanline renderer divides row/font/VRAM addresses. Keep the SDK
# hardware-divider entry points in SRAM so the IRQ never waits for XIP code.
target_compile_definitions(hdmi INTERFACE HDMI=1 PICO_DIVIDER_IN_RAM=1)

target_link_libraries(hdmi INTERFACE
        hardware_clocks
        hardware_dma
        hardware_gpio
        hardware_irq
        hardware_pio
        hardware_sync
)

target_include_directories(hdmi INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
