#!/bin/sh
set -e
TAG=fruitjam
BUILD=build

if ! [ -d lib/pico-sdk/lib/tinyusb/tools ]; then
    git submodule update --init
    (cd lib/pico-sdk && git submodule update --init lib/tinyusb && cd lib/tinyusb && ./tools/get_deps.py rp2040)
fi

export CFLAGS="-include $(pwd)/fruitjam_cflags.h -g3 -ggdb"
export CXXFLAGS="-include $(pwd)/fruitjam_cflags.h -g3 -ggdb"
cmake -S . -B $BUILD \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_PLATFORM=rp2350 \
    -DPICO_SDK_PATH=lib/pico-sdk \
    -DPICOTOOL_FETCH_FROM_GIT_PATH="$(pwd)/picotool" \
    -DBOARD=adafruit_fruit_jam -DPICO_BOARD=adafruit_fruit_jam \
    ${CMAKE_ARGS} "$@"

echo "Main course"
make -C $BUILD -j$(nproc)
