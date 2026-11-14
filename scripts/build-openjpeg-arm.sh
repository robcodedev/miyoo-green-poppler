#!/usr/bin/env bash
# Build OpenJPEG 2.4.0 target headers/CMake package for Poppler discovery.
# Runtime on the Miyoo should still use RedOnion's ABI-compatible libopenjp2.so.7.
set -euo pipefail
KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$KIT}"
SOURCE="${SOURCE:-$PROJECT_ROOT/openjpeg-2.4.0}"
STAGE="${OPJ:-$PROJECT_ROOT/openjpeg-arm}"
BUILDDIR="${BUILDDIR:-$SOURCE/build-arm}"
[[ -f "$SOURCE/CMakeLists.txt" ]] || { echo "Missing OpenJPEG 2.4.0 source: $SOURCE" >&2; exit 1; }
cmake -S "$SOURCE" -B "$BUILDDIR" \
  -DCMAKE_TOOLCHAIN_FILE="$KIT/toolchain/MiyooArmToolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_CODEC=OFF \
  -DBUILD_TESTING=OFF
cmake --build "$BUILDDIR" --parallel 2
# This syntax works with the older CMake on Debian Buster.
cmake --build "$BUILDDIR" --target install
find "$STAGE" -name OpenJPEGConfig.cmake -print
