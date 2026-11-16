add_library(ntsc-composite INTERFACE)

target_sources(ntsc-composite INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/ntsc-composite.c
        ${CMAKE_CURRENT_LIST_DIR}/graphics.c
)

target_compile_definitions(ntsc-composite INTERFACE NTSC_TV=1)

# Older graphics modules get their mode constants from the selected driver.
# Pre-include this public header so the NTSC folder can be copied on its own.
target_compile_options(ntsc-composite INTERFACE
        $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/ntsc-composite.h>
        $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/ntsc-composite.h>
)

# The default is GPIO28, set by ntsc-composite.h. A board which already uses
# that pin gives another one here; a command-line define beats every header
# default.
if (DEFINED NTSC_PIN_OUTPUT)
    target_compile_definitions(ntsc-composite INTERFACE NTSC_PIN_OUTPUT=${NTSC_PIN_OUTPUT})
endif()

option(NTSC_LOW_RAM "Use compact NTSC palette tables" OFF)
if (NTSC_LOW_RAM)
    target_compile_definitions(ntsc-composite INTERFACE NTSC_LOW_RAM=1)
endif()

option(NTSC_USE_SCRATCH_Y "Place NTSC palette tables in scratch Y" OFF)
if (NTSC_USE_SCRATCH_Y)
    target_compile_definitions(ntsc-composite INTERFACE NTSC_USE_SCRATCH_Y=1)
endif()

target_link_libraries(ntsc-composite INTERFACE
        graphics
        hardware_dma
        hardware_gpio
        hardware_irq
        hardware_pwm
        hardware_sync
)

target_include_directories(ntsc-composite INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
