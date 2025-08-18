#!/bin/bash

# Automated build script for all Pico-286 firmware variations.
# This script iterates through all platforms, display options, audio options,
# and memory configurations to build every possible firmware combination.

# Exit immediately if a command exits with a non-zero status.
set -e

# Set PICOTOOL_FETCH_FROM_GIT_PATH to /tmp to avoid issues with git dependencies
export PICOTOOL_FETCH_FROM_GIT_PATH=/tmp

# --- Build Configuration ---

# Platforms to build for
PLATFORMS=("rp2040" "rp2350")

# Display options (mutually exclusive)
DISPLAYS=("TFT" "VGA" "HDMI")

# Audio options (mutually exclusive)
AUDIOS=("I2S_SOUND" "PWM_SOUND" "HARDWARE_SOUND")

# Virtual Memory size in kilobytes to use for VM builds
VM_KBS="8192"

# Get the number of processor cores to use for parallel builds
NPROC=$(nproc)

# Build type for Pico builds is hardcoded to MinSizeRel in CMakeLists.txt
BUILD_TYPE="MinSizeRel"

# --- Release Directory Setup ---

RELEASE_DIR="release"

echo "Creating release directory: ${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}"

# --- Build Process ---

echo "Starting build process for all firmware variations..."

# Loop through each platform
for platform in "${PLATFORMS[@]}"; do
    build_dir="build-${platform}"
    echo "----------------------------------------------------------------"
    echo "Processing platform: ${platform} in build directory: ${build_dir}"
    echo "----------------------------------------------------------------"
    mkdir -p "${build_dir}"
    cd "${build_dir}"

    for display in "${DISPLAYS[@]}"; do
        for audio in "${AUDIOS[@]}"; do

            # --- Build with default external PSRAM ---
            echo "Building for ${platform} with ${display}, ${audio}, and default PSRAM..."
            rm -f CMakeCache.txt
            cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                  -DPICO_PLATFORM=${platform} \
                  -DENABLE_${display}=ON \
                  -DENABLE_${audio}=ON \
                  -DTOTAL_VIRTUAL_MEMORY_KBS=0 \
                  -DONBOARD_PSRAM=OFF \
                  ..
            make -j${NPROC}

            # --- Build with Virtual Memory (Swap File) ---
            echo "Building for ${platform} with ${display}, ${audio}, and Virtual Memory..."
            rm -f CMakeCache.txt
            cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                  -DPICO_PLATFORM=${platform} \
                  -DENABLE_${display}=ON \
                  -DENABLE_${audio}=ON \
                  -DTOTAL_VIRTUAL_MEMORY_KBS=${VM_KBS} \
                  -DONBOARD_PSRAM=OFF \
                  ..
            make -j${NPROC}

            # --- RP2350-specific builds ---
            if [ "${platform}" == "rp2350" ]; then
                # --- Build with Onboard PSRAM ---
                echo "Building for ${platform} with ${display}, ${audio}, and Onboard PSRAM..."
                rm -f CMakeCache.txt
                cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                      -DPICO_PLATFORM=${platform} \
                      -DENABLE_${display}=ON \
                      -DENABLE_${audio}=ON \
                      -DTOTAL_VIRTUAL_MEMORY_KBS=0 \
                      -DONBOARD_PSRAM=ON \
                      ..
                make -j${NPROC}
            fi
        done
    done
    cd ..
done

# --- Copy Artifacts to Release Directory ---

echo "----------------------------------------------------------------"
echo "Copying all firmware files to ${RELEASE_DIR}..."

for platform in "${PLATFORMS[@]}"; do
    if [ "${platform}" == "rp2350" ]; then
        src_dir="bin/rp2350-arm-s/${BUILD_TYPE}"
    else
        src_dir="bin/${platform}/${BUILD_TYPE}"
    fi

    if [ -d "${src_dir}" ]; then
        echo "Copying firmware from ${src_dir}"
        cp "${src_dir}"/*.uf2 "${RELEASE_DIR}/"
    else
        echo "Warning: Source directory ${src_dir} not found. Skipping copy."
    fi
done

echo "----------------------------------------------------------------"
echo "All builds completed successfully."

echo "Firmware files are in the '${RELEASE_DIR}' directory."

echo "----------------------------------------------------------------"
