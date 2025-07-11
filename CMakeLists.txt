# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if (WIN32)
    set(USERHOME $ENV{USERPROFILE})
else ()
    set(USERHOME $ENV{HOME})
endif ()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif ()
# ====================================================================================

cmake_minimum_required(VERSION 3.22)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(286 C CXX ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# =========================
# OUTPUT DIRECTORIES
# =========================
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${PICO_PLATFORM}/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

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
    add_executable(${PROJECT_NAME} ${SRC} src/win32-main.cpp src/WinMiniFB.c src/printf/printf.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
    target_include_directories(${PROJECT_NAME} PRIVATE src src/emu8950 src/printf)
else ()
    # =========================
    # PICO BUILD CONFIGURATION
    # =========================
    set(PICO_BOARD pico2 CACHE STRING "Board type")
    pico_sdk_init()

    # =========================
    # BUILD OPTIONS
    # =========================

    # Display Options (mutually exclusive)
    option(ENABLE_TFT "Enable TFT display output" OFF)
    option(ENABLE_VGA "Enable VGA display output" OFF)
    option(ENABLE_HDMI "Enable HDMI display output" OFF)

    # Audio Options (mutually exclusive)
    option(ENABLE_I2S_SOUND "Enable I2S audio output" OFF)
    option(ENABLE_PWM_SOUND "Enable PWM audio output" OFF)
    option(ENABLE_HARDWARE_SOUND "Enable hardware audio output" OFF)

    # Virtual Memory Configuration
    set(TOTAL_VIRTUAL_MEMORY_KBS "0" CACHE STRING "Total virtual memory in KBS. Set to 0 to use PSRAM instead (if PSRAM is otherwise enabled). Set to >0 to use virtual memory and disable all PSRAM.")
    # Determine if Virtual Memory is used instead of PSRAM
    if (TOTAL_VIRTUAL_MEMORY_KBS GREATER 0)
        set(USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM TRUE)
    else ()
        set(USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM FALSE)
    endif ()

    # PSRAM Configuration (onboard only available for RP2350)
    if (NOT PICO_PLATFORM STREQUAL "rp2040")
        option(ONBOARD_PSRAM "Enable onboard PSRAM support" ON)
        set(ONBOARD_PSRAM_GPIO "19" CACHE STRING "GPIO pin for onboard PSRAM")
    endif ()

    # CPU Configuration
    set(CPU_FREQ_MHZ "378" CACHE STRING "CPU frequency in MHz")
    
    set(ENABLE_PWM_SOUND ON)
    set(ENABLE_VGA ON)




    if (USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM)
        message(STATUS "TOTAL_VIRTUAL_MEMORY_KBS = ${TOTAL_VIRTUAL_MEMORY_KBS}K. Virtual Memory enabled, PSRAM will be disabled.")
        if (NOT PICO_PLATFORM STREQUAL "rp2040")
            # If ONBOARD_PSRAM was an option, ensure it's off when virtual memory is active.
            # The option itself is defined earlier, here we just force its value if virtual mem is on.
            set(ONBOARD_PSRAM OFF CACHE BOOL "Enable onboard PSRAM support" FORCE)
        endif ()
    endif ()

    # =========================
    # VALIDATION
    # =========================

    # Validate display options
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
        set(CPU_FREQ_MHZ "378")
        message(STATUS "HDMI enabled - CPU frequency locked to 378 MHz")
        math(EXPR DISPLAY_COUNT "${DISPLAY_COUNT} + 1")
    endif ()

    if (DISPLAY_COUNT EQUAL 0)
        message(FATAL_ERROR "At least one display option must be enabled")
    elseif (DISPLAY_COUNT GREATER 1)
        message(FATAL_ERROR "Only one display option can be enabled at a time")
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

    if (AUDIO_COUNT EQUAL 0)
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
        set(BUILD_NAME "${BUILD_NAME}-RP2040")
    else ()
        set(BUILD_NAME "${BUILD_NAME}-RP2350")
    endif ()

    # Add frequency
    set(BUILD_NAME "${BUILD_NAME}-${CPU_FREQ_MHZ}MHz")

    # Add display type
    if (ENABLE_TFT)
        set(BUILD_NAME "${BUILD_NAME}-TFT")
    elseif (ENABLE_VGA)
        set(BUILD_NAME "${BUILD_NAME}-VGA")
    elseif (ENABLE_HDMI)
        set(BUILD_NAME "${BUILD_NAME}-HDMI")
    endif ()

    # Add audio type
    if (ENABLE_I2S_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-I2S")
    elseif (ENABLE_PWM_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-PWM")
    elseif (ENABLE_HARDWARE_SOUND)
        set(BUILD_NAME "${BUILD_NAME}-HW")
    endif ()

    # Add PSRAM or Virtual Memory info
    if (USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM)
        set(BUILD_NAME "${BUILD_NAME}-SWAP${TOTAL_VIRTUAL_MEMORY_KBS}k")
        # Message for this case already handled when USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM was set
    else ()
        # This means TOTAL_VIRTUAL_MEMORY_KBS is 0, so PSRAM (if configured) is used.
        # ONBOARD_PSRAM will be OFF if platform is RP2040 or if user set it OFF.
        # It's only ON if platform is RP2350 and user set it ON (or default ON).
        if (ONBOARD_PSRAM AND NOT PICO_PLATFORM STREQUAL "rp2040")
            set(BUILD_NAME "${BUILD_NAME}-ONBOARD_PSRAM_PIN_${ONBOARD_PSRAM_GPIO}")
            message(STATUS "Onboard PSRAM enabled on GPIO ${ONBOARD_PSRAM_GPIO}.")
        else ()
            # Potentially generic PSRAM is active if drivers/psram was included.
            # We can check if psram target exists as a proxy or rely on ONBOARD_PSRAM being the main named feature.
            # For now, keep it simple: if not ONBOARD_PSRAM, no specific PSRAM string is added here by default,
            # unless drivers/psram itself adds something or we want a generic "-PSRAM" tag.
            # The original file only added ONBOARD_PSRAM specifics.
            message(STATUS "External PSRAM ON 18/19/20/21 pins")
        endif ()
    endif ()

    # =========================
    # EXECUTABLE SETUP
    # =========================
    add_compile_options(-flto -fwhole-program -ffast-math -ffunction-sections -fdata-sections -fms-extensions -O2)

    add_executable(${PROJECT_NAME} ${SRC} src/pico-main.c src/printf/printf.c src/74hc595/74hc595.c)
    target_include_directories(${PROJECT_NAME} PRIVATE src/74hc595 src src/emu8950 src/printf)
    target_sources(${PROJECT_NAME} PRIVATE src/emu8950/slot_render_pico.S)

    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${BUILD_NAME}")
    target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)

    # =========================
    # BOOT STAGE 2 CONFIGURATION
    # =========================
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})

    if (PICO_PLATFORM STREQUAL "rp2040")
        add_subdirectory(memops_opt)
        target_link_libraries(${PROJECT_NAME} PRIVATE memops_opt)
        target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    else ()
        target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=3)
    endif ()

    pico_set_boot_stage2(${PROJECT_NAME} slower_boot2)
    pico_set_program_name(${PROJECT_NAME} "PC 8086/8088/186/286 emulator")
    pico_set_program_version(${PROJECT_NAME} "wip")

    # =========================
    # SUBDIRECTORIES
    # =========================
    add_subdirectory(drivers/ps2)
    add_subdirectory(drivers/fatfs)
    add_subdirectory(drivers/sdcard)
    add_subdirectory(drivers/graphics)
    add_subdirectory(drivers/audio)
    add_subdirectory(drivers/psram)
    add_subdirectory(drivers/nespad)

    # Display drivers
    if (ENABLE_TFT)
        add_subdirectory(drivers/st7789)
        target_link_libraries(${PROJECT_NAME} PRIVATE st7789)
        message(STATUS "TFT display enabled")
    elseif (ENABLE_VGA)
        add_subdirectory(drivers/vga-nextgen)
        target_link_libraries(${PROJECT_NAME} PRIVATE vga-nextgen)
        message(STATUS "VGA display enabled")
    elseif (ENABLE_HDMI)
        add_subdirectory(drivers/hdmi)
        target_link_libraries(${PROJECT_NAME} PRIVATE hdmi)
        message(STATUS "HDMI display enabled")
    endif ()

    # =========================
    # LIBRARIES
    # =========================
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ps2 sdcard audio graphics nespad psram
            pico_runtime pico_stdlib pico_multicore
            hardware_dma hardware_pio hardware_i2c hardware_exception
            hardware_interp hardware_timer hardware_clocks hardware_pwm hardware_flash
    )

    # =========================
    # COMPILE DEFINITIONS
    # =========================
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            # Core definitions
            PICO_PANIC_FUNCTION=
            EMU8950_ASM=1
            CPU_FREQ_MHZ=${CPU_FREQ_MHZ}
            PICO_FLASH_SIZE_BYTES=16777216

            # Display type
            $<$<BOOL:${ENABLE_TFT}>:TFT>
            $<$<BOOL:${ENABLE_VGA}>:VGA>
            $<$<BOOL:${ENABLE_HDMI}>:HDMI>

            # Audio type
            $<$<BOOL:${ENABLE_I2S_SOUND}>:I2S_SOUND>
            $<$<BOOL:${ENABLE_PWM_SOUND}>:PWM_SOUND>
            $<$<BOOL:${ENABLE_HARDWARE_SOUND}>:HARDWARE_SOUND>

            # PSRAM / Virtual Memory Definitions
            $<$<BOOL:${USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM}>:TOTAL_VIRTUAL_MEMORY_KBS=${TOTAL_VIRTUAL_MEMORY_KBS}>
            # PSRAM related definitions (only if NOT using virtual memory)
            $<$<NOT:$<BOOL:${USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM}>>:PSRAM>
            $<$<NOT:$<BOOL:${USE_VIRTUAL_MEMORY_INSTEAD_OF_PSRAM}>>:$<$<AND:$<NOT:$<STREQUAL:${PICO_PLATFORM},rp2040>>,$<BOOL:${ONBOARD_PSRAM}>>:ONBOARD_PSRAM_GPIO=${ONBOARD_PSRAM_GPIO}>>

            # Pin configurations
            VGA_BASE_PIN=6 HDMI_BASE_PIN=6

            TFT_CS_PIN=6 TFT_RST_PIN=8 TFT_LED_PIN=9 TFT_DC_PIN=10 TFT_DATA_PIN=12 TFT_CLK_PIN=13

            SDCARD_PIN_SPI0_CS=5 SDCARD_PIN_SPI0_SCK=2 SDCARD_PIN_SPI0_MOSI=3 SDCARD_PIN_SPI0_MISO=4

            PSRAM_SPINLOCK=1 PSRAM_ASYNC=1 PSRAM_PIN_CS=18 PSRAM_PIN_SCK=19 PSRAM_PIN_MOSI=20 PSRAM_PIN_MISO=21

            USE_NESPAD NES_GPIO_CLK=14 NES_GPIO_DATA=16 NES_GPIO_LAT=15

            CLOCK_PIN=17 PCM_PIN=22
            PWM_BEEPER=28 PWM_LEFT_CHANNEL=26 PWM_RIGHT_CHANNEL=27
    )

    pico_add_extra_outputs(${PROJECT_NAME})
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