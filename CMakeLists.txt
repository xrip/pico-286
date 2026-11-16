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

#set(MURM20 ON)
#set(m1p2launcher ON)
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

    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
    target_include_directories(${PROJECT_NAME} PRIVATE src src/emu8950 src/printf)
else ()
    #set(CMAKE_BUILD_TYPE "MinSizeRel") /// outside!
    # =========================
    # PICO BUILD CONFIGURATION
    # =========================
    #set(PICO_BOARD pico2 CACHE STRING "Board type") // should be early
    if (PICO_PLATFORM STREQUAL "rp2350-arm-s")
        set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "Flash size in bytes" FORCE)
    endif ()
    pico_sdk_init()

    # =========================
    # BUILD OPTIONS
    # =========================

    # One primary output, optionally with NTSC composite.
    option(ENABLE_TFT "Enable TFT display output" OFF)
    option(ENABLE_VGA "Enable VGA display output" OFF)
    option(ENABLE_HDMI "Enable HDMI display output" OFF)
    option(M2P2_PIO_HDMI_TEST "Use PIO instead of HSTX for the M2P2 HDMI comparison" OFF)
    option(ENABLE_NTSC-TV "Enable NTSC TV output" OFF)
    set(M2P2 OFF)
    if (MURM20 AND PICO_PLATFORM STREQUAL "rp2350-arm-s")
        set(M2P2 ON)
    endif ()
    option(ONBOARD_PSRAM "Require onboard QMI PSRAM; omit SRAM swap cache" ${M2P2})
    if (ONBOARD_PSRAM AND NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
        message(FATAL_ERROR "ONBOARD_PSRAM requires RP2350 ARM")
    endif ()
    if (M2P2_PIO_HDMI_TEST AND (NOT M2P2 OR NOT ENABLE_HDMI OR ENABLE_NTSC-TV))
        message(FATAL_ERROR "M2P2_PIO_HDMI_TEST requires M2P2 HDMI without NTSC")
    endif ()

    # Audio Options (mutually exclusive)
    option(ENABLE_I2S_SOUND "Enable I2S audio output" OFF)
    option(ENABLE_PWM_SOUND "Enable PWM audio output" OFF)
    option(ENABLE_HARDWARE_SOUND "Enable hardware audio output" OFF)
    option(MIDI_GM_DLS "Use the GM.DLS wavetable MIDI engine" OFF)
    if (MIDI_GM_DLS)
        if (NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
            message(FATAL_ERROR "MIDI_GM_DLS requires the RP2350 Arm build with 16 MB flash")
        endif ()
        set(MIDI_GM_BANK "${CMAKE_SOURCE_DIR}/src/emulator/audio/gm_bank_44100.bin"
                CACHE FILEPATH "Packed GM.DLS v4 bank for 44100 Hz")
        if (NOT EXISTS "${MIDI_GM_BANK}")
            message(FATAL_ERROR "GM.DLS bank missing: ${MIDI_GM_BANK}. See BUILDING.md.")
        endif ()
        file(READ "${MIDI_GM_BANK}" MIDI_GM_HEADER LIMIT 12 HEX)
        if (NOT MIDI_GM_HEADER STREQUAL "474d57420400000044ac0000")
            message(FATAL_ERROR "MIDI_GM_BANK must be a GMWB v4 bank packed for 44100 Hz")
        endif ()
    endif ()

    # =========================
    # VALIDATION
    # =========================

    # M2P2 has HDMI on the RP2350 HSTX pins. RP2040 keeps PIO HDMI.
    set(HDMI_HSTX OFF)
    set(HDMI OFF)
    set(VGA ${ENABLE_VGA})
    set(TFT ${ENABLE_TFT})
    set(NTSC_TV ${ENABLE_NTSC-TV})
    if (ENABLE_HDMI)
        if (M2P2_PIO_HDMI_TEST)
            set(HDMI ON)
            message(STATUS "M2P2 HDMI backend: PIO comparison test")
        elseif (MURM20 AND NOT PICO_PLATFORM STREQUAL "rp2040")
            if (NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
                message(FATAL_ERROR "M2P2 HDMI requires RP2350 ARM HSTX (PICO_PLATFORM=rp2350-arm-s)")
            endif ()
            set(HDMI_HSTX ON)
            message(STATUS "M2P2 HDMI backend: HSTX")
        else ()
            set(HDMI ON)
        endif ()
    endif ()

    # Validate primary display options; NTSC can also run alone.
    set(DISPLAY_COUNT 0)
    if (ENABLE_TFT)
        message(STATUS "TFT Enabled")
        math(EXPR DISPLAY_COUNT "${DISPLAY_COUNT} + 1")
    endif ()
    if (ENABLE_VGA)
        message(STATUS "VGA Enabled")
        math(EXPR DISPLAY_COUNT "${DISPLAY_COUNT} + 1")
    endif ()
    if (ENABLE_HDMI)
        if (HDMI_HSTX)
            set(CPU_FREQ_MHZ "500")
        else ()
            set(CPU_FREQ_MHZ "504")
        endif ()
        math(EXPR DISPLAY_COUNT "${DISPLAY_COUNT} + 1")
    endif ()
    if (ENABLE_NTSC-TV)
        set(CPU_FREQ_MHZ "315")
        message(STATUS "NTSC clock: 315 MHz")
    endif ()

    if (DISPLAY_COUNT EQUAL 0 AND NOT ENABLE_NTSC-TV)
        message(FATAL_ERROR "At least one display option must be enabled")
    elseif (DISPLAY_COUNT GREATER 1)
        message(FATAL_ERROR "Select one of HDMI, VGA, or TFT; NTSC can be added")
    endif ()
    if (HDMI AND ENABLE_NTSC-TV)
        message(FATAL_ERROR "HDMI+NTSC needs M2P2 HSTX (MURM20=ON, PICO_BOARD=pico2)")
    endif ()
    set(NTSC_PIN_OUTPUT 22 CACHE STRING "NTSC composite GPIO")
    if (ENABLE_NTSC-TV AND ENABLE_HARDWARE_SOUND AND NTSC_PIN_OUTPUT EQUAL 22)
        message(FATAL_ERROR "NTSC GPIO22 conflicts with hardware sound PCM; select another NTSC_PIN_OUTPUT")
    endif ()

    # Validate audio options
    set(AUDIO_COUNT 0)
    if (ENABLE_I2S_SOUND)
        math(EXPR AUDIO_COUNT "${AUDIO_COUNT} + 1")
    endif ()
    if (ENABLE_PWM_SOUND)
        math(EXPR AUDIO_COUNT "${AUDIO_COUNT} + 1")
    endif ()
    if (ENABLE_HARDWARE_SOUND)
        math(EXPR AUDIO_COUNT "${AUDIO_COUNT} + 1")
    endif ()

    # HSTX without a chosen audio option sends the sound over HDMI.
    set(HDMI_SOUND OFF)
    if (AUDIO_COUNT EQUAL 0 AND HDMI_HSTX)
        set(HDMI_SOUND ON)
    elseif (AUDIO_COUNT EQUAL 0)
        message(FATAL_ERROR "At least one audio option must be enabled")
    elseif (AUDIO_COUNT GREATER 1)
        message(FATAL_ERROR "Only one audio option can be enabled at a time")
    endif ()

    # =========================
    # BUILD NAME GENERATION
    # =========================
    set(BUILD_NAME "${PROJECT_NAME}")

    # Add platform
    if (PICO_PLATFORM STREQUAL "rp2040")
        if (MURM20)
            set(BUILD_NAME "m2p1-${BUILD_NAME}")
        else ()
            set(BUILD_NAME "m1p1-${BUILD_NAME}")
        endif ()
    else ()
        if (MURM20)
            set(BUILD_NAME "m2p2-${BUILD_NAME}")
        else ()
            set(BUILD_NAME "m1p2-${BUILD_NAME}")
        endif ()
    endif ()

    # Add frequency
    if (PICO_PLATFORM STREQUAL "rp2040")
        string(APPEND BUILD_NAME "-F${FLASH_FREQ_MHZ}")
    endif ()
    string(APPEND BUILD_NAME "-P${PSRAM_FREQ_MHZ}-C${CPU_FREQ_MHZ}MHz")

    # Add display type
    if (ENABLE_TFT)
        set(BUILD_NAME "${BUILD_NAME}-TFT")
    elseif (ENABLE_VGA)
        set(BUILD_NAME "${BUILD_NAME}-VGA")
    elseif (HDMI_HSTX)
        set(BUILD_NAME "${BUILD_NAME}-HDMI-HSTX")
    elseif (ENABLE_HDMI)
        set(BUILD_NAME "${BUILD_NAME}-HDMI")
        if (M2P2_PIO_HDMI_TEST)
            string(APPEND BUILD_NAME "-PIO")
        endif ()
    endif ()
    if (ENABLE_NTSC-TV)
        string(APPEND BUILD_NAME "-NTSC")
    endif ()

    # Add audio type
    if (ENABLE_I2S_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-I2S")
    elseif (ENABLE_PWM_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-PWM")
    elseif (ENABLE_HARDWARE_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-HW")
    elseif (HDMI_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-HDMIAUDIO")
    endif ()
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

    add_executable(${PROJECT_NAME} ${SRC} src/pico-main.c src/printf/printf.c src/74hc595/74hc595.c)
    target_include_directories(${PROJECT_NAME} PRIVATE src/74hc595 src src/emu8950 src/printf)
    target_sources(${PROJECT_NAME} PRIVATE src/emu8950/slot_render_pico.S)
    if (MIDI_GM_DLS)
        target_compile_definitions(${PROJECT_NAME} PRIVATE MIDI_GM_DLS=1)
        configure_file(src/emulator/audio/gm_dls_bank.S.in gm_dls_bank.S @ONLY)
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gm_dls_bank.S")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/gm_dls_bank.S"
                PROPERTIES OBJECT_DEPENDS "${MIDI_GM_BANK}")
    endif ()

    # =========================
    # BOOT STAGE 2 CONFIGURATION
    # =========================
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    if (M2P2)
        # The RP2350 ROM finds a working read mode. main() then enables QE
        # and sets quad-read timing from RAM before raising the system clock.
        target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_EMBED_XIP_SETUP=0)
    endif ()
    if (ENABLE_HDMI AND NOT M2P2 AND PICO_PLATFORM STREQUAL "rp2350-arm-s")
        target_compile_definitions(slower_boot2 PRIVATE
                PICO_FLASH_SPI_CLKDIV=4 PICO_FLASH_SPI_RXDELAY=4)
        target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_EMBED_XIP_SETUP=1)
    endif ()

    if (PICO_PLATFORM STREQUAL "rp2040")
        add_subdirectory(memops_opt)
        target_link_libraries(${PROJECT_NAME} PRIVATE memops_opt)
        target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    else ()
        if (PICO_PLATFORM STREQUAL "rp2350-arm-s")
            target_compile_definitions(${PROJECT_NAME} PRIVATE
                    PICO_STACK_SIZE=4096 PICO_CORE1_STACK_SIZE=4096
                    PICO_USE_STACK_GUARDS=1)
        endif ()
        if (m1p2launcher)
            if (MURM20)
                set(BUILD_NAME "${BUILD_NAME}.m2p2")
            else ()
                set(BUILD_NAME "${BUILD_NAME}.m1p2")
            endif ()
            pico_set_linker_script(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/memmap12.ld")
        else ()
            pico_set_linker_script(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/memmap.ld")
        endif ()
    endif ()

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
    add_subdirectory(drivers/audio)
    add_subdirectory(drivers/psram)
    add_subdirectory(drivers/nespad)
    if (M2P2)
        add_subdirectory(drivers/flash-config)
        target_link_libraries(${PROJECT_NAME} PRIVATE flash-config)
        target_compile_definitions(${PROJECT_NAME} PRIVATE M2P2_FLASH_CONFIG=1)
    endif ()

    # The emulator owns VRAM and fixes clocks before launching core 1.
    set(GRAPHICS_BUILTIN_FRAMEBUFFER OFF)
    if (HDMI_HSTX)
        set(GRAPHICS_NO_CLOCK_SETUP OFF)
        pico_enable_stdio_usb(${PROJECT_NAME} 0)
        pico_enable_stdio_uart(${PROJECT_NAME} 0)
    else ()
        set(GRAPHICS_NO_CLOCK_SETUP ON)
    endif ()
    if (M2P2_PIO_HDMI_TEST)
        pico_enable_stdio_usb(${PROJECT_NAME} 1)
        pico_enable_stdio_uart(${PROJECT_NAME} 0)
        target_compile_definitions(${PROJECT_NAME} PRIVATE
                PC_USB_CONSOLE=1 PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)
    endif ()
    if (HDMI_HSTX OR M2P2_PIO_HDMI_TEST)
        target_sources(${PROJECT_NAME} PRIVATE src/fault_trace.c)
    endif ()
    add_subdirectory(drivers/graphics)
    if (ENABLE_TFT)
        add_subdirectory(drivers/st7789)
        target_link_libraries(${PROJECT_NAME} PRIVATE st7789)
    endif ()
    if (ENABLE_VGA)
        add_subdirectory(drivers/vga)
        target_link_libraries(${PROJECT_NAME} PRIVATE vga)
    endif ()
    if (ENABLE_NTSC-TV)
        add_subdirectory(drivers/ntsc-composite)
        target_link_libraries(${PROJECT_NAME} PRIVATE ntsc-composite)
    endif ()
    if (HDMI_HSTX)
        add_subdirectory(drivers/hdmi-hstx)
        target_link_libraries(${PROJECT_NAME} PRIVATE hdmi-hstx)
    elseif (HDMI)
        add_subdirectory(drivers/hdmi)
        target_link_libraries(${PROJECT_NAME} PRIVATE hdmi)
    endif ()

    # =========================
    # LIBRARIES
    # =========================
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ps2 sdcard audio graphics nespad psram
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
            # Core definitions
            PICO_PANIC_FUNCTION=
            EMU8950_ASM=1
            CPU_FREQ_MHZ=${CPU_FREQ_MHZ}
            FLASH_FREQ_MHZ=${FLASH_FREQ_MHZ}
            PSRAM_FREQ_MHZ=${PSRAM_FREQ_MHZ}
            PICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES}

            # Display macros come from the selected driver targets.
            GRAPHICS_PC_EMULATOR=1
            $<$<BOOL:${ONBOARD_PSRAM}>:ONBOARD_PSRAM=1>

            # Audio type
            $<$<BOOL:${ENABLE_I2S_SOUND}>:I2S_SOUND>
            $<$<BOOL:${ENABLE_PWM_SOUND}>:PWM_SOUND>
            $<$<BOOL:${ENABLE_HARDWARE_SOUND}>:HARDWARE_SOUND>
            $<$<BOOL:${HDMI_SOUND}>:HDMI_SOUND>

            PSRAM
    )
    if (MURM20)
        target_compile_definitions(${PROJECT_NAME} PRIVATE
                MURM2=1
                # Pin configurations
                MOUSE_CLOCK_PIN=0 MOUSE_DATA_PIN=1
                KBD_CLOCK_PIN=2 KBD_DATA_PIN=3
                VGA_BASE_PIN=12 HDMI_BASE_PIN=12
                TFT_CS_PIN=12 TFT_RST_PIN=14 TFT_LED_PIN=15 TFT_DC_PIN=16 TFT_DATA_PIN=18 TFT_CLK_PIN=19
                SDCARD_PIN_SPI0_CS=5 SDCARD_PIN_SPI0_SCK=6 SDCARD_PIN_SPI0_MOSI=7 SDCARD_PIN_SPI0_MISO=4
                PSRAM_SPINLOCK=1 PSRAM_ASYNC=1 PSRAM_PIN_CS=18 PSRAM_PIN_SCK=19 PSRAM_PIN_MOSI=20 PSRAM_PIN_MISO=21
                USE_NESPAD NES_GPIO_CLK=20 NES_GPIO_DATA=21 NES_GPIO_LAT=26
                CLOCK_PIN=27 PCM_PIN=22
                PWM_BEEPER=9 PWM_LEFT_CHANNEL=10 PWM_RIGHT_CHANNEL=11
        )
    else ()
        target_compile_definitions(${PROJECT_NAME} PRIVATE
                # Pin configurations
                MOUSE_CLOCK_PIN=14 MOUSE_DATA_PIN=15
                KBD_CLOCK_PIN=0 KBD_DATA_PIN=1
                VGA_BASE_PIN=6 HDMI_BASE_PIN=6
                TFT_CS_PIN=6 TFT_RST_PIN=8 TFT_LED_PIN=9 TFT_DC_PIN=10 TFT_DATA_PIN=12 TFT_CLK_PIN=13
                SDCARD_PIN_SPI0_CS=5 SDCARD_PIN_SPI0_SCK=2 SDCARD_PIN_SPI0_MOSI=3 SDCARD_PIN_SPI0_MISO=4
                PSRAM_SPINLOCK=1 PSRAM_ASYNC=1 PSRAM_PIN_CS=18 PSRAM_PIN_SCK=19 PSRAM_PIN_MOSI=20 PSRAM_PIN_MISO=21
                USE_NESPAD NES_GPIO_CLK=14 NES_GPIO_DATA=16 NES_GPIO_LAT=15
                CLOCK_PIN=17 PCM_PIN=22
                PWM_BEEPER=28 PWM_LEFT_CHANNEL=26 PWM_RIGHT_CHANNEL=27
        )
    endif ()
    pico_add_extra_outputs(${PROJECT_NAME})

    if (m1p2launcher)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rename
            "${OUTPUT_DIR}/${BUILD_NAME}.uf2"
            "${OUTPUT_DIR}/${BUILD_NAME}"
        )
    endif ()
endif ()

# =========================
# COMMON DEFINITIONS (HOST + PICO)
# =========================
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
