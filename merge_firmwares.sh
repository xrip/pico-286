#!/bin/bash

# This script merges the RP2040 and RP2350 firmware files.

# Exit immediately if a command exits with a non-zero status.
set -e

RELEASE_DIR="release"

if [ ! -d "${RELEASE_DIR}" ]; then
    echo "Error: Release directory '${RELEASE_DIR}' not found."
    echo "Please run the build script first."
    exit 1
fi

cd "${RELEASE_DIR}"

echo "Merging RP2040 and RP2350 firmware files..."

# Loop through all RP2040 firmware files
for rp2040_file in 286-RP2040-*.uf2; do
    # Construct the corresponding RP2350 filename
    rp2350_file="${rp2040_file/RP2040/RP2350}"

    # Check if the RP2350 file exists
    if [ -f "${rp2350_file}" ]; then
        # Construct the merged filename
        merged_file="${rp2040_file/RP2040-/}"

        echo "Merging ${rp2040_file} and ${rp2350_file} into ${merged_file}..."

        # Concatenate the files (RP2040 first)
        cat "${rp2040_file}" "${rp2350_file}" > "${merged_file}"

        # Remove the original files
        rm "${rp2040_file}" "${rp2350_file}"
    fi
done

echo "----------------------------------------------------------------"
echo "Firmware merging completed successfully."
echo "Merged files are in the '${RELEASE_DIR}' directory."
echo "----------------------------------------------------------------"

cd ..
