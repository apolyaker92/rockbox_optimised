#!/bin/bash
#
# Build this branch for the iPod Video (5th gen).
#
#   tools/build-ipod.sh deps        install build dependencies (Debian/Ubuntu, needs root)
#   tools/build-ipod.sh toolchain   build the ARM cross compiler (about 30 minutes)
#   tools/build-ipod.sh firmware    build rockbox.zip for the iPod
#   tools/build-ipod.sh sim         build the desktop simulator
#
# Environment:
#   RBDEV_PREFIX  where the toolchain goes      (default: ~/rbdev)
#   BUILD_DIR     where builds go                (default: ~/rockbox-build)
#
# The toolchain downloads GCC and binutils from mirrors.kernel.org and
# gcc.gnu.org, so those hosts must be reachable.

set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
RBDEV_PREFIX=${RBDEV_PREFIX:-$HOME/rbdev}
BUILD_DIR=${BUILD_DIR:-$HOME/rockbox-build}
export PATH="$RBDEV_PREFIX/bin:$PATH"

deps() {
    apt-get update
    apt-get install -y build-essential git perl zip texinfo flex bison \
        libtool libtool-bin libgmp-dev libmpfr-dev libmpc-dev libsdl2-dev
}

toolchain() {
    mkdir -p "$RBDEV_PREFIX"
    cd "$SRC/tools"
    RBDEV_PREFIX="$RBDEV_PREFIX" \
    RBDEV_BUILD="$BUILD_DIR/toolchain-build" \
    RBDEV_DOWNLOAD="$BUILD_DIR/toolchain-download" \
        ./rockboxdev.sh --target=a
}

firmware() {
    if ! command -v arm-elf-eabi-gcc > /dev/null; then
        echo "arm-elf-eabi-gcc not found, run: $0 toolchain" >&2
        exit 1
    fi
    rm -rf "$BUILD_DIR/ipod" && mkdir -p "$BUILD_DIR/ipod" && cd "$BUILD_DIR/ipod"
    "$SRC/tools/configure" --target=ipodvideo --type=n --ram=64 < /dev/null
    make -j"$(nproc)"
    make zip
    echo
    echo "Built $BUILD_DIR/ipod/rockbox.zip"
    unzip -p rockbox.zip .rockbox/rockbox-info.txt | grep Version
}

sim() {
    rm -rf "$BUILD_DIR/sim" && mkdir -p "$BUILD_DIR/sim" && cd "$BUILD_DIR/sim"
    "$SRC/tools/configure" --target=ipodvideo --type=s --ram=64 < /dev/null
    make -j"$(nproc)"
    make install
    echo
    echo "Run it with: cd $BUILD_DIR/sim && ./rockboxui"
    echo "Music goes in $BUILD_DIR/sim/simdisk"
}

case "$1" in
    deps)      deps ;;
    toolchain) toolchain ;;
    firmware)  firmware ;;
    sim)       sim ;;
    *)
        sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
