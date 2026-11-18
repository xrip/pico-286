if (NOT (DEFINED PICO_PLATFORM AND PICO_PLATFORM STREQUAL "host"))
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
endif()
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull in Raspberry Pi Pico SDK (must be before project, but after PICO_BOARD)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(286 C CXX ASM)
SET(BUILD_NAME "${PROJECT_NAME}")

# CPU Configuration
set(CPU_FREQ_MHZ "500")
set(FLASH_FREQ_MHZ "100")
set(PSRAM_FREQ_MHZ "166")

# =========================
# OUTPUT DIRECTORIES
# =========================
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${PICO_PLATFORM}/${CMAKE_BUILD_TYPE}")
# SDK helper files stay in each build tree. Only final firmware goes to bin,
# so separate video builds cannot overwrite each other's boot-stage files.

# =========================
# SOURCE FILES
# =========================
file(GLOB_RECURSE SRC "src/emulator/*.cpp" "src/emulator/*.c" "src/emu8950/*.c" "src/emu8950/*.cpp")

message(STATUS "Add source files:")
foreach (SRC_FILE IN LISTS SRC)
    message(STATUS "${SRC_FILE}")
endforeach ()
message(STATUS "")

# =========================
# MIDI OPTIONS (default: the sine/noise engine)
# =========================
option(MIDI_GM_DLS "Use the GM.DLS wavetable MIDI engine" OFF)
option(MIDI_WINMM "Win32: send MIDI to the Windows MIDI output (winmm)" OFF)
if (MIDI_WINMM)
    if (NOT (PICO_PLATFORM STREQUAL "host" AND WIN32))
        message(FATAL_ERROR "MIDI_WINMM requires the Win32 build")
    endif ()
    if (MIDI_GM_DLS)
        message(FATAL_ERROR "MIDI_WINMM and MIDI_GM_DLS cannot both be ON")
    endif ()
endif ()
if (MIDI_GM_DLS)
    if (NOT (PICO_PLATFORM STREQUAL "rp2350-arm-s" OR (PICO_PLATFORM STREQUAL "host" AND WIN32)))
        message(FATAL_ERROR "MIDI_GM_DLS requires the RP2350 Arm build with 16 MB flash or the Win32 build")
    endif ()
    set(MIDI_GM_BANK "${CMAKE_SOURCE_DIR}/src/emulator/audio/gm_bank_22050.bin"
            CACHE FILEPATH "Packed GM.DLS v4 bank for 22050 Hz")
    if (NOT EXISTS "${MIDI_GM_BANK}")
        message(FATAL_ERROR "GM.DLS bank missing: ${MIDI_GM_BANK}. See BUILDING.md.")
    endif ()
    file(READ "${MIDI_GM_BANK}" MIDI_GM_HEADER LIMIT 12 HEX)
    if (NOT MIDI_GM_HEADER STREQUAL "474d57420400000022560000")
        message(FATAL_ERROR "MIDI_GM_BANK must be a GMWB v4 bank packed for 22050 Hz")
    endif ()
endif ()

# =========================
# HOST BUILD (Windows/Linux)
# =========================
if (PICO_PLATFORM STREQUAL "host")
    add_compile_options(-fms-extensions -O2)

    if (WIN32)
        add_executable(${PROJECT_NAME} ${SRC} src/win32-main.cpp src/WinMiniFB.c src/printf/printf.c)
        target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
    else ()
        # Linux build
        add_executable(${PROJECT_NAME} ${SRC} src/linux-main.cpp src/LinuxMiniFB.c src/linux-audio.c src/printf/printf.c findfirst/findfirst.c findfirst/spec.c)
        target_link_libraries(${PROJECT_NAME} PRIVATE X11 pthread)
        target_include_directories(${PROJECT_NAME} PRIVATE src src/emu8950 src/printf findfirst/)
    endif ()

    if (MIDI_GM_DLS)
        string(APPEND BUILD_NAME "-GMDLS")
    elseif (MIDI_WINMM)
        string(APPEND BUILD_NAME "-WINMM")
    endif ()
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}" OUTPUT_NAME "${BUILD_NAME}")
    target_include_directories(${PROJECT_NAME} PRIVATE src src/emu8950 src/printf)
else ()
    # =========================
    # PICO BUILD: RP2350 (M2P2) with HSTX HDMI, HDMI audio and 8 MB QMI PSRAM
    # =========================
    if (NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
        message(FATAL_ERROR "Only PICO_PLATFORM=rp2350-arm-s (M2P2) or host is supported")
    endif ()
    set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "Flash size in bytes" FORCE)
    pico_sdk_init()

    set(BUILD_NAME "${PROJECT_NAME}-P${PSRAM_FREQ_MHZ}-C${CPU_FREQ_MHZ}MHz")
    if (MIDI_GM_DLS)
        string(APPEND BUILD_NAME "-GMDLS")
    endif ()

    # =========================
    # EXECUTABLE SETUP
    # =========================
    add_link_options(-flto)
    # -frename-registers -fno-tree-vectorize
    add_compile_options(
            -flto=auto
            -frename-registers
            -fomit-frame-pointer
            #        -ffreestanding // build issues with std::malloc/std::free in SDK 2.1+
            -ffast-math
            -ffunction-sections
            -fdata-sections
            -fms-extensions
            -O2
    )

    add_executable(${PROJECT_NAME} ${SRC} src/pico-main.c src/printf/printf.c src/fault_trace.c)
    target_include_directories(${PROJECT_NAME} PRIVATE src src/emu8950 src/printf)
    target_sources(${PROJECT_NAME} PRIVATE src/emu8950/slot_render_pico.S)

    # =========================
    # BOOT STAGE 2 CONFIGURATION
    # =========================
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    # The RP2350 ROM finds a working read mode. main() then enables QE
    # and sets quad-read timing from RAM before raising the system clock.
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_EMBED_XIP_SETUP=0
            PICO_STACK_SIZE=4096 PICO_CORE1_STACK_SIZE=4096 PICO_USE_STACK_GUARDS=1)
    pico_set_linker_script(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/memmap.ld")

    set_target_properties(${PROJECT_NAME} PROPERTIES
            OUTPUT_NAME "${BUILD_NAME}" RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
    target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)

    pico_set_program_name(286 "PC 8086/8088/186/286 emulator")
    pico_set_program_version(${PROJECT_NAME} "1.1.0")
    pico_set_boot_stage2(${PROJECT_NAME} slower_boot2)

    # =========================
    # SUBDIRECTORIES
    # =========================
    add_subdirectory(drivers/ps2)
    add_subdirectory(drivers/fatfs)
    add_subdirectory(drivers/sdcard)
    add_subdirectory(drivers/nespad)
    add_subdirectory(drivers/flash-config)
    add_subdirectory(drivers/graphics)
    add_subdirectory(drivers/hstx-vga-hdmi-audio)
    pico_enable_stdio_usb(${PROJECT_NAME} 0)
    pico_enable_stdio_uart(${PROJECT_NAME} 0)

    # =========================
    # LIBRARIES
    # =========================
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ps2 sdcard graphics hstx-vga-hdmi-audio nespad flash-config
            pico_runtime pico_stdlib pico_multicore
            hardware_dma hardware_pio hardware_i2c hardware_exception
            hardware_interp hardware_timer hardware_clocks hardware_pwm hardware_flash
            -Wl,--wrap=atexit,--wrap=abort,--gc-sections
    )
    pico_set_printf_implementation(${PROJECT_NAME} none)

    # =========================
    # COMPILE DEFINITIONS
    # =========================
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            PICO_PANIC_FUNCTION=
            EMU8950_ASM=1
            CPU_FREQ_MHZ=${CPU_FREQ_MHZ}
            FLASH_FREQ_MHZ=${FLASH_FREQ_MHZ}
            PSRAM_FREQ_MHZ=${PSRAM_FREQ_MHZ}
            PICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES}
            # Pins
            MOUSE_CLOCK_PIN=0 MOUSE_DATA_PIN=1
            KBD_CLOCK_PIN=2 KBD_DATA_PIN=3
            SDCARD_PIN_SPI0_CS=5 SDCARD_PIN_SPI0_SCK=6 SDCARD_PIN_SPI0_MOSI=7 SDCARD_PIN_SPI0_MISO=4
            NES_GPIO_CLK=20 NES_GPIO_DATA=21 NES_GPIO_LAT=26
    )
    pico_add_extra_outputs(${PROJECT_NAME})
endif ()

# =========================
# COMMON DEFINITIONS (HOST + PICO)
# =========================
if (MIDI_WINMM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MIDI_WINMM=1)
endif ()
if (MIDI_GM_DLS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MIDI_GM_DLS=1)
    configure_file(src/emulator/audio/gm_dls_bank.S.in gm_dls_bank.S @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gm_dls_bank.S")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/gm_dls_bank.S"
            PROPERTIES OBJECT_DEPENDS "${MIDI_GM_BANK}")
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE
        USE_EMU8950_OPL
        EMU8950_SLOT_RENDER=1
        EMU8950_NO_RATECONV=1
        EMU8950_NO_WAVE_TABLE_MAP=1
        EMU8950_NO_TLL=1
        EMU8950_NO_FLOAT=1
        EMU8950_NO_TIMER=1
        EMU8950_NO_TEST_FLAG=1
        EMU8950_SIMPLER_NOISE=1
        EMU8950_SHORT_NOISE_UPDATE_CHECK=1
        EMU8950_LINEAR_SKIP=1
        EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION
        EMU8950_NO_PERCUSSION_MODE=1
        EMU8950_LINEAR=1
)
